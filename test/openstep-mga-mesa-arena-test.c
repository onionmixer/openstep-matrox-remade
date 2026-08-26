/*
 * Is there room for a texture at the display's own size?
 *
 * The offscreen window used to be three megabytes, and at 1024x768 the colour
 * surface and the space reserved for depth used all of it -- so the texture
 * arena was empty and every textured scene at that size fell back to
 * software, for no reason except that nobody had established how much memory
 * the board has.
 *
 * This asks the back end directly, at three sizes, and says how many textures
 * of a few ordinary sizes would fit.  It draws nothing.
 */
#include <stdio.h>
#include <stdlib.h>
#include <GL/gl.h>
#include <GL/osmesa.h>
#include "../mesa/OpenStepMGAMesaBuffer.h"
#include "../mesa/OpenStepMGAMesaProbe.h"
#include "OpenStepMGAHW3D.h"

static void
askAt(int w, int h)
{
    OSMesaContext ctx;
    unsigned long *app;
    unsigned long org = 0UL, bytes = 0UL;
    int got;

    app = (unsigned long *)malloc((unsigned)(w * h) * sizeof *app);
    if (!app) { printf("   %dx%d: no room\n", w, h); return; }
    ctx = OSMesaCreateContext(OSMESA_ARGB, NULL);
    if (!ctx || !OSMesaMakeCurrent(ctx, app, GL_UNSIGNED_BYTE, w, h)) {
        printf("   %dx%d: no context\n", w, h);
        free(app);
        return;
    }
    if (OSMGAMesaBufferOrigin() == 0UL) {
        printf("   %-9s NOT the engine's surface\n", "");
        OSMesaDestroyContext(ctx); free(app); return;
    }
    got = OSMGAMesaBufferTextureArena((const void *)ctx, &org, &bytes);
    printf("   %4dx%-4d  surface at %8lu  arena %s",
           w, h, OSMGAMesaBufferOrigin(),
           got ? "" : "NONE");
    if (got)
        printf("at %8lu, %7lu bytes  (%.2f MiB) -- room for %lu 256x256, "
               "%lu 512x512",
               org, bytes, (double)bytes / (1024.0 * 1024.0),
               bytes / (256UL * 256UL * 4UL), bytes / (512UL * 512UL * 4UL));
    printf("\n");
    OSMesaDestroyContext(ctx);
    free(app);
}

/*
 * And one thing asserted rather than reported.
 *
 * The arena at the display's own size is what widening the window bought, and
 * a report nobody reads would not notice the day it goes back to nothing.
 * The assertion is written against the WINDOW rather than against a number
 * for this board: if the window is big enough to hold a colour surface, the
 * depth beside it and something over, then something over is what there must
 * be.  On a board where the window stays small this says so and does not
 * fail.
 */
static int
insist(void)
{
    OSMesaContext ctx;
    unsigned long *app;
    unsigned long org = 0UL, bytes = 0UL, window;
    int got, ok;

    app = (unsigned long *)malloc((unsigned)(1024 * 768) * sizeof *app);
    if (!app) return 0;
    ctx = OSMesaCreateContext(OSMESA_ARGB, NULL);
    if (!ctx || !OSMesaMakeCurrent(ctx, app, GL_UNSIGNED_BYTE, 1024, 768)) {
        printf("   no context\n");
        free(app);
        return 1;
    }
    if (OSMGAMesaBufferOrigin() == 0UL) {
        printf("   1024x768 is not on the engine's surface -- nothing to"
               " insist on\n");
        OSMesaDestroyContext(ctx); free(app);
        return 0;
    }
    got = OSMGAMesaBufferTextureArena((const void *)ctx, &org, &bytes);
    {
        /* The window's size as the driver publishes it -- the back end has no
         * accessor for it, and the capability is where it comes from. */
        OSMGAMesaProbe pr;

        OSMGAMesaProbeRun(&pr);
        window = pr.caps[OSMGA_HW3D_CAP_VRAMLEN];
    }
    /* colour is 1024*768*4, depth half that; anything past the two is arena */
    ok = (window <= (unsigned long)(1024 * 768) * 6UL) ? 1
       : (got && bytes > 0UL);
    printf("   window %lu bytes; colour+depth need %lu; arena %s -> %s\n",
           window, (unsigned long)(1024 * 768) * 6UL,
           got ? "present" : "NONE", ok ? "as it should be" : "WRONG");
    OSMesaDestroyContext(ctx);
    free(app);
    return ok ? 0 : 1;
}

int
main(void)
{
    int bad;

    printf("how much room a texture has, at each size\n\n");
    askAt(512, 384);
    askAt(800, 600);
    askAt(1024, 768);
    /*
     * The two the widened window made possible.  1024x768 was the display's
     * own size when this was written; at 1600x1200 the question is whether a
     * surface the size of the SCREEN binds at all -- which is the whole point
     * of a 32 MB declaration, and which no test asked before.
     */
    askAt(1280, 1024);
    askAt(1600, 1200);
    printf("\n");
    bad = insist();
    printf("   %d failed\n", bad);
    return bad;
}
