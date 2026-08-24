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

int
main(void)
{
    printf("how much room a texture has, at each size\n\n");
    askAt(512, 384);
    askAt(800, 600);
    askAt(1024, 768);
    return 0;
}
