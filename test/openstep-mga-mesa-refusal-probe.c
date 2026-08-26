/*
 * WHY was the batch refused?
 *
 * The teapot at 1280x1024 and 1600x1200 has the driver refusing every
 * submission until the library gives up and revokes acceleration; at
 * 1024x768 and below it draws.  The driver does not log a reason -- the
 * submit path returns IO_R_INVALID_ARG and moves on -- but it DOES hand the
 * verdict back in the submission block, and the back end keeps the last one.
 *
 * So this draws one triangle at a size given on the command line and prints
 * what came back.  It exists because the alternative was guessing which of
 * eighteen E_ codes it is from the shape of the arithmetic, and the guess
 * would have been the 7 MiB constant, which turns out to be in test files
 * only.
 *
 *    refusal 1280x1024
 */

#include <stdio.h>
#include <stdlib.h>
#include <GL/gl.h>
#include <GL/osmesa.h>
#include "../mesa/OpenStepMGAMesaHook.h"
#include "../mesa/OpenStepMGAMesaBuffer.h"
#include "../mesa/OpenStepMGAMesaProbe.h"
#include "OpenStepMGAHW3D.h"

/* The same geometry the teapot draws, cut from the Mesa tree at build time. */
#include "teapot-geometry.h"

static const char *
verdictName(unsigned long v)
{
    switch (v) {
    case OSMGA_HW3D_OK:        return "OK";
    case OSMGA_HW3D_E_MAGIC:   return "E_MAGIC";
    case OSMGA_HW3D_E_VERSION: return "E_VERSION";
    case OSMGA_HW3D_E_COUNT:   return "E_COUNT";
    case OSMGA_HW3D_E_DSTORG:  return "E_DSTORG";
    case OSMGA_HW3D_E_ZORG:    return "E_ZORG";
    case OSMGA_HW3D_E_TEXORG:  return "E_TEXORG";
    case OSMGA_HW3D_E_DWGCTL:  return "E_DWGCTL";
    case OSMGA_HW3D_E_TRIROW:  return "E_TRIROW";
    case OSMGA_HW3D_E_TRICOL:  return "E_TRICOL";
    case OSMGA_HW3D_E_TRISLOPE:return "E_TRISLOPE";
    case OSMGA_HW3D_E_ALPHA:   return "E_ALPHA";
    case OSMGA_HW3D_E_TEXSIZE: return "E_TEXSIZE";
    case OSMGA_HW3D_E_TEXCOORD:return "E_TEXCOORD";
    case OSMGA_HW3D_E_DSTSIZE: return "E_DSTSIZE";
    case OSMGA_HW3D_E_EDGEDIV: return "E_EDGEDIV";
    case OSMGA_HW3D_E_DSTPITCH:return "E_DSTPITCH";
    case OSMGA_HW3D_E_TRICROSS:return "E_TRICROSS";
    case OSMGA_HW3D_E_TRISGN:  return "E_TRISGN";
    case OSMGA_HW3D_E_TRIEMPTY:return "E_TRIEMPTY";
    default:                   return "unknown";
    }
}

int
main(int argc, char **argv)
{
    OSMesaContext ctx;
    unsigned long *buf;
    const OSMGAMesaRefusal *r;
    OSMGAMesaProbe probe;
    int w = (argc > 1) ? atoi(argv[1]) : 1280;
    int h = (argc > 2) ? atoi(argv[2]) : 1024;
    unsigned long v;

    if (w <= 0 || h <= 0 || w > 2048 || h > 2048) {
        printf("size out of range\n");
        return 2;
    }
    buf = (unsigned long *)malloc((unsigned)(w * h) * sizeof *buf);
    if (!buf) { printf("no room for %dx%d\n", w, h); return 2; }

    ctx = OSMesaCreateContext(OSMESA_ARGB, NULL);
    if (!ctx || !OSMesaMakeCurrent(ctx, buf, GL_UNSIGNED_BYTE, w, h)) {
        printf("no context at %dx%d\n", w, h);
        return 2;
    }
    OSMGAMesaProbeRun(&probe);
    printf("at %dx%d\n", w, h);
    printf("   probe verdict        : %d\n", (int)probe.verdict);
    printf("   surface origin       : %lu\n", OSMGAMesaBufferOrigin());
    printf("   surface stride       : %lu\n", OSMGAMesaBufferStride());
    printf("   depth origin         : %lu\n", OSMGAMesaBufferDepthOrigin());
    printf("   window offset/length : %lu / %lu\n",
           probe.caps[OSMGA_HW3D_CAP_VRAMOFF],
           probe.caps[OSMGA_HW3D_CAP_VRAMLEN]);
    printf("   display stride cap   : %lu\n",
           probe.caps[OSMGA_HW3D_CAP_STRIDE]);

    /*
     * Depth ON, because that is the difference between this and the teapot.
     * A single flat triangle with no depth test drew fine at 1280x1024, so
     * whatever is refused is not the colour surface's size.
     */
    glViewport(0, 0, w, h);
    glEnable(GL_DEPTH_TEST);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClearDepth(1.0);
    glClear((GLbitfield)(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT));
    /*
     * The teapot itself, coarsely.  One flat triangle draws fine at every
     * size, so whatever the driver refuses needs the evaluator surfaces --
     * many small smooth-shaded trapezoids with depth -- not a big flat one.
     * grid 4 rather than 12 because this is a diagnosis, not a picture.
     */
    {
        GLfloat pos[4];

        pos[0] = 0.0f; pos[1] = 5.0f; pos[2] = 10.0f; pos[3] = 1.0f;
        glEnable(GL_LIGHTING);
        glEnable(GL_LIGHT0);
        glEnable(GL_AUTO_NORMAL);
        glEnable(GL_NORMALIZE);
        glLightfv(GL_LIGHT0, GL_POSITION, pos);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glFrustum(-1.0, 1.0, -0.75, 0.75, 2.0, 20.0);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        glTranslatef(0.0f, -0.3f, -6.0f);
        teapot(4, 1.0, GL_FILL);
    }
    glFinish();

    /*
     * Named for what each one counts.  "refused" was ambiguous and meant two
     * different things in two places: this counter is the back end declining
     * BEFORE submitting, while an actual kernel refusal shows up in the
     * verdict counts below.  Telling them apart is the whole point after the
     * boundary preflight went in -- the numbers move from one to the other.
     */
    printf("\n   drawn by the card       : %lu\n", OSMGAMesaHookDrawn());
    printf("   drawn by Mesa           : %lu\n", OSMGAMesaHookSoftware());
    printf("   declined before submit  : %lu\n",
           OSMGAMesaHookUnsupported());

    r = OSMGAMesaHookLastRefusal();
    if (r != 0 && r->verdict != 0UL) {
        printf("\n   LAST KERNEL REFUSAL\n");
        printf("      verdict   : %lu (%s)\n", r->verdict,
               verdictName(r->verdict));
        printf("      status    : %lu\n", r->status);
        printf("      triangle  : %lu of %lu\n", r->triangle, r->triCount);
        printf("      dst       : %lux%lu\n", r->dstWidth, r->dstHeight);
        /*
         * The two 16-bit halves of FXBNDRY are what E_TRICOL judges:
         * refused when either exceeds the surface width or when the left
         * one is past the right.  Printed raw as well, because a negative
         * x packed into an unsigned half is the shape this is looking for.
         */
        printf("      fxbndry   : %08lx  left=%lu right=%lu (width %lu)\n",
               r->tri.fxbndry,
               r->tri.fxbndry & 0xFFFFUL,
               (r->tri.fxbndry >> 16) & 0xFFFFUL,
               r->dstWidth);
        printf("      ar0/ar6   : %ld / %ld\n", r->tri.ar0, r->tri.ar6);
        printf("      ar1/ar2   : %ld / %ld\n", r->tri.ar1, r->tri.ar2);
        printf("      ar4/ar5   : %ld / %ld\n", r->tri.ar4, r->tri.ar5);
        printf("      sgn       : %ld\n", r->tri.sgn);
    } else {
        printf("\n   no kernel refusal recorded\n");
    }
    for (v = 0UL; v < OSMGA_MESA_VERDICTS; v++)
        if (OSMGAMesaHookVerdictCount(v) != 0UL)
            printf("      count[%lu %-10s] = %lu\n", v, verdictName(v),
                   OSMGAMesaHookVerdictCount(v));
    OSMesaDestroyContext(ctx);
    free(buf);
    return 0;
}
