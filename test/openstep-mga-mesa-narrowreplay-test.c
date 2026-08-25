/*
 * One refused sliver must cost one software triangle, not the batch.
 *
 * The scene is thirty ordinary tiles and one sliver that the validator
 * refuses as TRICROSS -- a frozen vertex triple found by driving the REAL
 * builder and the REAL validator with a deterministic search
 * (scratchpad/crossfind.c), not by hoping one falls out of a scene.  The
 * triple is exact in 1/256 units; Mesa's float transform can drift a
 * coordinate by up to a step, so the sliver is tried at a few sub-pixel
 * offsets and the test refuses to pass unless one of them actually
 * triggered the refusal it is about.
 *
 * Two drawings are compared:
 *
 *   batch limit 180   one batch; the refusal must be NARROWED -- prefix to
 *                     the engine, the sliver alone to software, remainder
 *                     to the engine
 *   batch limit 1     every triangle its own batch; the sliver alone is
 *                     refused and replayed, the rest were always hardware
 *
 * The tiles do not overlap the sliver or each other, so both drawings must
 * be BYTE IDENTICAL: hardware pixels for every tile, software pixels for
 * the sliver.  Before the narrowing, the first drawing replayed all thirty
 * tiles in software and the two images differed wherever hardware and
 * software rasterisation disagree.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "GL/gl.h"
#include "GL/osmesa.h"
#include "../mesa/OpenStepMGAMesaHook.h"

#define W 320
#define H 240

static unsigned long *buf;

/* crossfind find #2, 1/256 px: a near-vertical sliver 87 rows tall whose
 * independently rounded edges cross partway down */
static const double SAX = 51666.0 / 256.0, SAY = 15032.0 / 256.0;
static const double SBX = 51604.0 / 256.0, SBY = 18505.0 / 256.0;
static const double SCX = 51308.0 / 256.0, SCY = 37311.0 / 256.0;

static void
scene(double off)
{
    int i;

    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glBegin(GL_TRIANGLES);
    for (i = 0; i < 30; i++) {
        double x = 8.0 + (double)(i % 10) * 18.0;
        double y = 8.0 + (double)(i / 10) * 18.0;

        /* 15 tiles, then the sliver, then the rest: it must sit in the
         * MIDDLE of the batch so both a prefix and a remainder exist */
        if (i == 15) {
            glColor3f(1.0f, 1.0f, 0.0f);
            glVertex3d(SAX + off, SAY, 0.0);
            glVertex3d(SBX + off, SBY, 0.0);
            glVertex3d(SCX + off, SCY, 0.0);
        }
        glColor3f(0.2f + 0.02f * (double)i, 0.8f, 0.3f);
        glVertex3d(x, y, 0.0);
        glVertex3d(x + 14.0, y, 0.0);
        glVertex3d(x + 6.0, y + 14.0, 0.0);
    }
    glEnd();
    glFinish();
}

int
main(void)
{
    OSMesaContext ctx;
    unsigned long *keep;
    unsigned long d0, r0, n0, drew, replayed, narrowed;
    double off = 0.0;
    int k, i, diff;

    buf = (unsigned long *)malloc((unsigned)(W * H) * sizeof *buf);
    keep = (unsigned long *)malloc((unsigned)(W * H) * sizeof *keep);
    if (!buf || !keep) { printf("no room\n"); return 2; }
    ctx = OSMesaCreateContext(OSMESA_ARGB, NULL);
    if (!ctx || !OSMesaMakeCurrent(ctx, buf, GL_UNSIGNED_BYTE, W, H)) {
        printf("no context\n"); return 2;
    }
    glViewport(0, 0, W, H);
    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    glOrtho(0.0, (double)W, 0.0, (double)H, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    glDisable(GL_BLEND); glDisable(GL_DITHER); glDisable(GL_TEXTURE_2D);
    glDisable(GL_CULL_FACE); glDisable(GL_DEPTH_TEST);
    glShadeModel(GL_FLAT);

    printf("one refused sliver against a thirty-tile batch\n\n");

    /* find the offset at which the sliver really refuses */
    for (k = 0; k < 8; k++) {
        off = (double)k / 1024.0;
        n0 = OSMGAMesaHookNarrowed();
        scene(off);
        if (OSMGAMesaHookNarrowed() != n0) break;
    }
    if (k >= 8) {
        printf("NOT TRIGGERED: no offset made the sliver refuse -- the "
               "frozen triple no longer reaches TRICROSS\n");
        printf("NARROWREPLAY FAIL\n");
        return 1;
    }
    printf("  sliver refuses at offset %d/1024 px\n", k);

    /* the drawing under test: one batch, narrowed */
    d0 = OSMGAMesaHookDrawn(); r0 = OSMGAMesaHookReplayed();
    n0 = OSMGAMesaHookNarrowed();
    scene(off);
    drew = OSMGAMesaHookDrawn() - d0;
    replayed = OSMGAMesaHookReplayed() - r0;
    narrowed = OSMGAMesaHookNarrowed() - n0;
    memcpy(keep, buf, (unsigned)(W * H) * sizeof *buf);
    printf("  limit 180: drawn %lu, replayed %lu, narrowed %lu "
           "(want 30, 1, 1)\n", drew, replayed, narrowed);

    /* the reference: every triangle alone; only the sliver is refused */
    OSMGAMesaHookBatchLimit(1UL);
    scene(off);
    OSMGAMesaHookBatchLimit(180UL);

    diff = 0;
    for (i = 0; i < W * H; i++)
        if (keep[i] != buf[i]) diff++;
    printf("  against limit 1: %d pixel(s) differ (want 0)\n", diff);

    if (drew == 30UL && replayed == 1UL && narrowed >= 1UL && diff == 0) {
        printf("\nNARROWREPLAY PASS\n");
        return 0;
    }
    printf("\nNARROWREPLAY FAIL\n");
    return 1;
}
