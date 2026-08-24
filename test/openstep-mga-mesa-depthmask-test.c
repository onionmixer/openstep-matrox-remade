/*
 * glDepthMask(GL_FALSE) -- comparing without writing -- against software.
 *
 * The engine has no depth write mask.  It has an ACCESS TYPE: ZI compares
 * and writes, I compares and does not.  Matrox's own decoder says as much
 * (xf86-video-mga-2.0.0/util/stormdwg.c:32 and :35) and the raw probe asked
 * the card, but neither of those is this: what is checked here is that the
 * GL state a client actually sets comes out right through the whole path.
 *
 * Three questions per depth function, and the third is the one that matters:
 *
 *   1. does a fragment that should be discarded still get discarded
 *   2. does a fragment that should survive still get drawn
 *   3. after a masked draw that SURVIVED, is the depth buffer unchanged --
 *      asked not by reading depth but by drawing a THIRD quad that only
 *      passes against the original depth.  A driver that wrote the depth
 *      anyway would fail that one and nothing else.
 *
 * The depth is read as well, because it is shared with the engine here and
 * a direct answer is worth having; but the third quad is what would catch a
 * write the readback could not see.
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
static OSMesaContext theCtx;
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

static void
flatQuad(double z, float r, float g, float b)
{
    glColor3f(r, g, b);
    glBegin(GL_TRIANGLES);
      glVertex3d(20.0, 20.0, z); glVertex3d(108.0, 20.0, z);
      glVertex3d(108.0, 76.0, z);
      glVertex3d(20.0, 20.0, z); glVertex3d(108.0, 76.0, z);
      glVertex3d(20.0, 76.0, z);
    glEnd();
}

#define PX_R(p) (((p) >> 16) & 0xFFUL)
#define PX_G(p) (((p) >>  8) & 0xFFUL)
#define PX_B(p) ( (p)        & 0xFFUL)

/*
 * Seed at zseed with the mask ON, then draw at zprobe with the mask OFF,
 * then draw a witness at zwitness with the mask off as well.
 *
 * Returns three bits: whether the probe won the pixel, whether the witness
 * won it afterwards, and whether the depth code under the pixel moved.
 */
static void
run(GLenum func, double zseed, double zprobe, double zwitness, int soft,
    int *probeWon, int *witnessWon, int *depthMoved, unsigned long *drew)
{
    unsigned long before;
    unsigned short *zb;
    GLint dw, dh, bpv;
    unsigned short zAfterSeed = 0, zAtEnd = 0;
    void *raw;

    glDepthMask(GL_TRUE);
    glDepthFunc(GL_ALWAYS);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClearDepth(1.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (soft) softOn();
    flatQuad(zseed, 1.0f, 0.0f, 0.0f);          /* red seed */
    glFinish();

    (void)OSMesaGetDepthBuffer(theCtx, &dw, &dh, &bpv, &raw);
    zb = (unsigned short *)raw;
    zAfterSeed = zb[48 * W + 64];

    glDepthFunc(func);
    glDepthMask(GL_FALSE);
    before = OSMGAMesaHookDrawn();
    flatQuad(zprobe, 0.0f, 1.0f, 0.0f);         /* green probe */
    glFinish();
    *drew = OSMGAMesaHookDrawn() - before;
    *probeWon = (PX_G(app[48 * W + 64]) > 0x80UL);

    /*
     * The witness.  It sits between the seed and the probe, so it beats the
     * seed's depth and loses to the probe's -- which means it is drawn if
     * and only if the probe did NOT write its depth.  This is the question
     * a readback cannot be trusted to answer alone.
     */
    flatQuad(zwitness, 0.0f, 0.0f, 1.0f);       /* blue witness */
    glFinish();
    *witnessWon = (PX_B(app[48 * W + 64]) > 0x80UL);

    glDepthMask(GL_TRUE);
    if (soft) softOff();

    zAtEnd = zb[48 * W + 64];
    *depthMoved = (zAtEnd != zAfterSeed);
}

int
main(void)
{
    OSMesaContext ctx;
    int i;
    static const struct { GLenum f; const char *n; } cases[3] = {
        { GL_LESS,    "GL_LESS   " },
        { GL_GREATER, "GL_GREATER" },
        { GL_LEQUAL,  "GL_LEQUAL " }
    };

    app = (unsigned long *)malloc((unsigned)(W * H) * sizeof(unsigned long));
    if (!app) { printf("no room\n"); return 2; }
    ctx = OSMesaCreateContext(OSMESA_ARGB, NULL);
    if (!ctx || !OSMesaMakeCurrent(ctx, app, GL_UNSIGNED_BYTE, W, H)) {
        printf("no context\n"); return 2;
    }
    theCtx = ctx;
    {
        void *zb; GLint dw, dh, bpv;

        if (!OSMesaGetDepthBuffer(ctx, &dw, &dh, &bpv, &zb) || !zb ||
            bpv != 2) {
            printf("NOT RUN: no 16-bit depth buffer\n");
            return 2;
        }
        /*
         * And it has to be the buffer the ENGINE writes, not a private one
         * Mesa kept for itself -- otherwise every depth number below is the
         * software rasteriser's and the test is about nothing.
         */
        if (OSMGAMesaBufferDepthOrigin() == 0UL) {
            printf("NOT RUN: the depth buffer is not shared with the "
                   "engine\n");
            return 2;
        }
    }
    glViewport(0, 0, W, H);
    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    glOrtho(0.0, (double)W, 0.0, (double)H, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND); glDisable(GL_DITHER);
    glDisable(GL_TEXTURE_2D); glDisable(GL_CULL_FACE);
    glDisable(GL_ALPHA_TEST);
    glShadeModel(GL_FLAT);

    printf("glDepthMask(GL_FALSE), engine against software\n\n");

    /*
     * glOrtho(0, W, 0, H, -1, 1) puts the window depth at (1 - z)/2, so a
     * LARGER z is NEARER.  The seed sits in the middle; the probe is on
     * whichever side the function needs; the witness is always between the
     * seed and the probe.
     */
    for (i = 0; i < 3; i++) {
        double zseed = 0.0;
        double zprobe = (cases[i].f == GL_GREATER) ? -0.6 : 0.6;
        double zwitness = (cases[i].f == GL_GREATER) ? -0.3 : 0.3;
        int hp, hw, hm, sp, sw, sm;
        unsigned long dh, ds;
        char name[96];

        run(cases[i].f, zseed, zprobe, zwitness, 0, &hp, &hw, &hm, &dh);
        run(cases[i].f, zseed, zprobe, zwitness, 1, &sp, &sw, &sm, &ds);

        printf("   %s  engine probe %d witness %d depthmoved %d   "
               "software %d %d %d\n",
               cases[i].n, hp, hw, hm, sp, sw, sm);

        sprintf(name, "%s: the probe passes the comparison", cases[i].n);
        say(name, hp == 1);
        sprintf(name, "%s: and writes no depth, so the witness still draws",
                cases[i].n);
        say(name, hw == 1);
        sprintf(name, "%s: and the depth code under the pixel did not move",
                cases[i].n);
        say(name, hm == 0);
        sprintf(name, "%s: software agrees on all three", cases[i].n);
        say(name, hp == sp && hw == sw && hm == sm);
        if (dh == 0UL) {
            printf("   FAIL  %s never reached the engine\n", cases[i].n);
            failures++;
        }
        if (ds != 0UL) {
            printf("   FAIL  %s: the software pass was accelerated\n",
                   cases[i].n);
            failures++;
        }
    }

    /*
     * GL_ALWAYS with the mask off, which is the odd one out.
     *
     * Its z mode encodes as NOUGHT, and nought with access type I is exactly
     * what "this triangle has no depth" has always looked like -- so the
     * kernel classifies it as not addressing depth and hands it the scratch
     * depth origin instead of the real one.  That is still the right
     * picture: a comparison that always passes and a write that never
     * happens is a triangle that draws and leaves depth alone.  But it is
     * the one combination where the driver reaches the right answer by a
     * different route, so it is asked for separately rather than assumed.
     */
    printf("\n   GL_ALWAYS with the mask off\n");
    {
        void *raw; unsigned short *zb; GLint dw, dh2, bpv;
        unsigned short z0, z1;
        unsigned long before, drew;

        glDepthMask(GL_TRUE);
        glDepthFunc(GL_ALWAYS);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        flatQuad(0.0, 1.0f, 0.0f, 0.0f);
        glFinish();
        (void)OSMesaGetDepthBuffer(theCtx, &dw, &dh2, &bpv, &raw);
        zb = (unsigned short *)raw;
        z0 = zb[48 * W + 64];

        glDepthMask(GL_FALSE);
        before = OSMGAMesaHookDrawn();
        flatQuad(-0.6, 0.0f, 1.0f, 0.0f);   /* farther, but ALWAYS passes */
        glFinish();
        drew = OSMGAMesaHookDrawn() - before;
        z1 = zb[48 * W + 64];
        glDepthMask(GL_TRUE);

        printf("   depth %04x -> %04x\n", z0, z1);
        say("GL_ALWAYS draws even from farther away",
            PX_G(app[48 * W + 64]) > 0x80UL);
        say("and moves no depth", z1 == z0);
        say("and it was the engine that drew it", drew != 0UL);
    }

    /*
     * The other half of the contract: with the mask ON, the depth MUST move.
     * Without this the whole test above is also passed by a driver that
     * quietly stopped writing depth altogether.
     */
    printf("\n   and with the mask on again\n");
    {
        void *raw; unsigned short *zb; GLint dw, dh2, bpv;
        unsigned short z0, z1;
        unsigned long before, drew;

        glDepthMask(GL_TRUE);
        glDepthFunc(GL_ALWAYS);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        flatQuad(0.0, 1.0f, 0.0f, 0.0f);
        glFinish();
        (void)OSMesaGetDepthBuffer(theCtx, &dw, &dh2, &bpv, &raw);
        zb = (unsigned short *)raw;
        z0 = zb[48 * W + 64];

        glDepthFunc(GL_LESS);
        before = OSMGAMesaHookDrawn();
        flatQuad(0.6, 0.0f, 1.0f, 0.0f);
        glFinish();
        drew = OSMGAMesaHookDrawn() - before;
        z1 = zb[48 * W + 64];

        printf("   depth %04x -> %04x\n", z0, z1);
        say("an unmasked draw still moves the depth", z1 != z0);
        say("and it was the engine that drew it", drew != 0UL);
    }

    /*
     * The reason the feature exists: a transparency pass.
     *
     * Translucent surfaces are drawn with the depth test ON, so they are
     * hidden by opaque geometry in front of them, and the depth mask OFF, so
     * they do not hide EACH OTHER.  With the mask on, whichever translucent
     * quad is drawn first writes its depth and rejects the ones behind it,
     * and the picture loses every layer but one.
     *
     * The check is which layers survived, not what colour came out.  The
     * engine's blend arithmetic rounds once where Mesa's truncates, so the
     * two paths differ by a level here and there by design; whether the blue
     * quad is in the picture at all does not.
     */
    printf("\n   a transparency pass, which is what this is for\n");
    {
        int soft;
        int blueSeen[2];
        unsigned long drew[2];

        for (soft = 0; soft < 2; soft++) {
            unsigned long before;

            glDepthMask(GL_TRUE);
            glDepthFunc(GL_LESS);
            glDisable(GL_BLEND);
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glClearDepth(1.0);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            if (soft) softOn();

            /* the opaque wall, farthest back */
            flatQuad(-0.5, 1.0f, 0.0f, 0.0f);
            glFinish();

            /* the translucent layers, both in front of it, nearer first --
             * the order that would go wrong if depth were written */
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glDepthMask(GL_FALSE);
            before = OSMGAMesaHookDrawn();
            glColor4f(0.0f, 1.0f, 0.0f, 0.5f);
            glBegin(GL_TRIANGLES);
              glVertex3d(20.0, 20.0, 0.6); glVertex3d(108.0, 20.0, 0.6);
              glVertex3d(108.0, 76.0, 0.6);
              glVertex3d(20.0, 20.0, 0.6); glVertex3d(108.0, 76.0, 0.6);
              glVertex3d(20.0, 76.0, 0.6);
            glEnd();
            glColor4f(0.0f, 0.0f, 1.0f, 0.5f);
            glBegin(GL_TRIANGLES);
              glVertex3d(20.0, 20.0, 0.2); glVertex3d(108.0, 20.0, 0.2);
              glVertex3d(108.0, 76.0, 0.2);
              glVertex3d(20.0, 20.0, 0.2); glVertex3d(108.0, 76.0, 0.2);
              glVertex3d(20.0, 76.0, 0.2);
            glEnd();
            glFinish();
            drew[soft] = OSMGAMesaHookDrawn() - before;

            glDepthMask(GL_TRUE);
            glDisable(GL_BLEND);
            if (soft) softOff();

            blueSeen[soft] = (PX_B(app[48 * W + 64]) > 0x20UL);
            printf("   %s: pixel %06lx, blue layer %s\n",
                   soft ? "software" : "engine  ",
                   app[48 * W + 64] & 0xFFFFFFUL,
                   blueSeen[soft] ? "present" : "MISSING");
        }

        say("the farther translucent layer is not lost", blueSeen[0] == 1);
        say("and software agrees", blueSeen[0] == blueSeen[1]);
        if (drew[0] == 0UL) {
            printf("   FAIL  the transparency pass never reached the "
                   "engine\n");
            failures++;
        }
        if (drew[1] != 0UL) {
            printf("   FAIL  the software pass was accelerated\n");
            failures++;
        }
    }

    printf("\n%s (%d failing)\n",
           failures ? "=== PROBLEM ===" : "=== nothing to report ===",
           failures);
    return failures ? 1 : 0;
}
