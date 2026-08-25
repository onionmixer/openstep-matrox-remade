/*
 * What a refused batch replays with.
 *
 * The replay contract (OpenStepMGAMesaHook.c:352-368) keeps the VB INDICES
 * alive across a batch.  It says nothing about the context state the
 * software rasteriser reads, and vbrender.c mutates some of that around
 * every single triangle callback rather than around the bracket:
 * ctx->PolygonZoffset is computed at vbrender.c:298-304 and zeroed again at
 * 324-328, and Mesa's triangle template adds it to the vertex Z
 * (tritemp.h:718).
 *
 * The replay does not run inside that callback.  It runs from
 * osmgaMesaFlushPending, which RenderFinish calls after the callbacks have
 * returned -- by which time the offset is nought.
 *
 * So this draws one offset quad three ways and reads the depth it left:
 *
 *   hw      the engine takes the batch; the hook applies its own offset
 *   replay  the batch is refused on purpose, so software redraws it
 *   soft    the chooser is bypassed; Mesa draws it, offset and all
 *
 * "soft" is the reference.  "hw" already matches it -- that is what the
 * polygon-offset test checks.  If "replay" does not, the contract has a
 * hole in it, and the number it reports is the depth of an unoffset quad.
 *
 * Injection is turned on only around the drawing.  A refused batch bumps a
 * consecutive-refusal count that revokes the whole probe at eight
 * (OSMGA_MESA_REFUSAL_LIMIT), and a taken batch resets it -- so letting the
 * clear through between cases keeps every case accelerated.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "GL/gl.h"
#include "GL/osmesa.h"
#include "../mesa/OpenStepMGAMesaHook.h"

#define W 128
#define H 96

static unsigned long *app;
static OSMesaContext theCtx;

enum { PATH_HW = 0, PATH_REPLAY = 1, PATH_SOFT = 2 };

static void
quad(double zl, double zr)
{
    glColor3f(1.0f, 1.0f, 1.0f);
    glBegin(GL_TRIANGLES);
      glVertex3d(20.0, 20.0, zl); glVertex3d(108.0, 20.0, zr);
      glVertex3d(108.0, 76.0, zr);
      glVertex3d(20.0, 20.0, zl); glVertex3d(108.0, 76.0, zr);
      glVertex3d(20.0, 76.0, zl);
    glEnd();
}

/* the depth left at the middle pixel, and how many triangles were replayed */
static int
depthAt(double zl, double zr, GLfloat factor, GLfloat units, int path,
        unsigned long *replayed)
{
    void *zb; GLint dw, dh, bpv;
    unsigned long before;

    glEnable(GL_DEPTH_TEST); glDepthFunc(GL_ALWAYS); glDepthMask(GL_TRUE);
    glClearDepth(1.0);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glFinish();                 /* the clear goes through TAKEN, resetting
                                 * the consecutive-refusal count */
    before = OSMGAMesaHookReplayed();

    glPolygonOffset(factor, units);
    glEnable(GL_POLYGON_OFFSET_FILL);
    if (path == PATH_SOFT)   OSMGAMesaHookForceSoftware(1);
    if (path == PATH_REPLAY) OSMGAMesaHookInjectRefusal(1);
    quad(zl, zr);
    glFinish();
    if (path == PATH_REPLAY) OSMGAMesaHookInjectRefusal(0);
    if (path == PATH_SOFT)   OSMGAMesaHookForceSoftware(0);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glDisable(GL_DEPTH_TEST);

    *replayed = OSMGAMesaHookReplayed() - before;
    if (!OSMesaGetDepthBuffer(theCtx, &dw, &dh, &bpv, &zb))
        return -1;
    return (int)((unsigned short *)zb)[48 * W + 64];
}

/*
 * The other half of the same hole: two-side lighting.
 *
 * Mesa points ColorPtr, IndexPtr and Specular at the front or the back array
 * by this triangle's facing (vbrender.c:306-311) and puts them back to front
 * before the bracket closes (vbrender.c:714-718).  So a batch replayed at
 * the bracket would draw a back face in the FRONT face's colours.
 *
 * The triangle below is wound clockwise while front is counter-clockwise, so
 * it is a back face and must come out in the back material's colour.
 */
static unsigned long
backFaceColour(int path, unsigned long *drew, unsigned long *replayed)
{
    unsigned long d0, r0;
    static const GLfloat black[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    static const GLfloat front[4] = { 1.0f, 0.0f, 0.0f, 1.0f };  /* red */
    static const GLfloat back[4]  = { 0.0f, 0.0f, 1.0f, 1.0f };  /* blue */

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glFinish();

    glEnable(GL_LIGHTING);
    glLightModeli(GL_LIGHT_MODEL_TWO_SIDE, GL_TRUE);
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, black);
    glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT, black);
    glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, black);
    glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, black);
    glMaterialfv(GL_FRONT, GL_EMISSION, front);
    glMaterialfv(GL_BACK,  GL_EMISSION, back);
    glFrontFace(GL_CCW);
    glDisable(GL_CULL_FACE);
    d0 = OSMGAMesaHookDrawn();
    r0 = OSMGAMesaHookReplayed();

    if (path == PATH_SOFT)   OSMGAMesaHookForceSoftware(1);
    if (path == PATH_REPLAY) OSMGAMesaHookInjectRefusal(1);
    glBegin(GL_TRIANGLES);                     /* clockwise: a back face */
      glNormal3f(0.0f, 0.0f, 1.0f);
      glVertex3d(20.0, 20.0, 0.0);
      glVertex3d(64.0, 76.0, 0.0);
      glVertex3d(108.0, 20.0, 0.0);
    glEnd();
    glFinish();
    if (path == PATH_REPLAY) OSMGAMesaHookInjectRefusal(0);
    if (path == PATH_SOFT)   OSMGAMesaHookForceSoftware(0);

    glDisable(GL_LIGHTING);
    glLightModeli(GL_LIGHT_MODEL_TWO_SIDE, GL_FALSE);
    *drew = OSMGAMesaHookDrawn() - d0;
    *replayed = OSMGAMesaHookReplayed() - r0;
    return app[40 * W + 64] & 0x00ffffffUL;
}

int
main(void)
{
    OSMesaContext ctx;
    static const struct { GLfloat f, u; const char *what; } cases[] = {
        { 0.0f,    0.0f,  "no offset at all" },
        { 0.0f, 1024.0f,  "units only" },
        { 0.0f, -1024.0f, "units only, the other way" },
        { 1.0f,  256.0f,  "factor and units" }
    };
    int bad = 0, n;

    app = (unsigned long *)malloc((unsigned)(W * H) * sizeof(unsigned long));
    if (!app) { printf("no room\n"); return 2; }
    ctx = OSMesaCreateContext(OSMESA_ARGB, NULL);
    if (!ctx || !OSMesaMakeCurrent(ctx, app, GL_UNSIGNED_BYTE, W, H)) {
        printf("no context\n"); return 2;
    }
    theCtx = ctx;
    {
        void *zb; GLint dw, dh, bpv;
        if (!OSMesaGetDepthBuffer(ctx, &dw, &dh, &bpv, &zb) || bpv != 2) {
            printf("NOT RUN: no 16-bit depth buffer\n"); return 2;
        }
    }
    glViewport(0, 0, W, H);
    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    glOrtho(0.0, (double)W, 0.0, (double)H, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    glDisable(GL_BLEND); glDisable(GL_DITHER); glDisable(GL_TEXTURE_2D);
    glDisable(GL_CULL_FACE); glDisable(GL_ALPHA_TEST);
    glShadeModel(GL_FLAT);

    printf("what a refused batch replays with\n\n");
    printf("  %-26s %8s %8s %8s  %s\n",
           "case", "hw", "replay", "soft", "replayed");

    for (n = 0; n < (int)(sizeof cases / sizeof cases[0]); n++) {
        unsigned long rh, rr, rs;
        int dh, dr, ds;

        dh = depthAt(0.5, 0.5, cases[n].f, cases[n].u, PATH_HW,     &rh);
        dr = depthAt(0.5, 0.5, cases[n].f, cases[n].u, PATH_REPLAY, &rr);
        ds = depthAt(0.5, 0.5, cases[n].f, cases[n].u, PATH_SOFT,   &rs);

        printf("  %-26s %8d %8d %8d  %lu\n",
               cases[n].what, dh, dr, ds, rr);
        if (dh != ds) { printf("      hw differs from software\n"); bad++; }
        if (dr != ds) { printf("      REPLAY differs from software\n"); bad++; }
        if (cases[n].u != 0.0f && rr == 0UL) {
            printf("      NOT TESTED: nothing was replayed\n");
            bad++;
        }
        (void)rh; (void)rs;
    }

    {
        unsigned long dh, rh, dr, rr2, ds, rs;
        unsigned long ch = backFaceColour(PATH_HW,     &dh, &rh);
        unsigned long cr = backFaceColour(PATH_REPLAY, &dr, &rr2);
        unsigned long cs = backFaceColour(PATH_SOFT,   &ds, &rs);

        printf("\n  %-26s %8s %8s %8s\n", "back face, two-side lit",
               "hw", "replay", "soft");
        printf("  %-26s %08lx %08lx %08lx\n", "", ch, cr, cs);
        if (ch != cs) { printf("      hw differs from software\n"); bad++; }
        if (cr != cs) { printf("      REPLAY differs from software\n"); bad++; }
        /* A case that never reached the engine would agree with software for
         * the wrong reason, so say what each path actually did. */
        printf("  %-26s drawn %lu, replayed %lu\n", "  the engine took", dh, rh);
        if (dh == 0UL) { printf("      NOT TESTED: hw never accelerated it\n"); bad++; }
        if (rr2 == 0UL) { printf("      NOT TESTED: nothing was replayed\n"); bad++; }
    }

    printf("\n%s\n", bad ? "REPLAYSTATE FAIL" : "REPLAYSTATE PASS");
    OSMesaDestroyContext(ctx);
    free(app);
    return bad ? 1 : 0;
}
