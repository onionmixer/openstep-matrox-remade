/*
 * glReadPixels into the caller's own OSMesa array.
 *
 * A read is bracketed by RenderStart and RenderFinish exactly as drawing is,
 * and this back end mirrors the whole surface at RenderFinish.  So a read
 * used to cost a full copy -- and if the caller aimed the read at the very
 * array OSMesa was given, the copy landed on top of the result, writing the
 * surface's own 32-bit words over the packing the caller had asked for.
 *
 * That aliasing is legal.  Nothing in GL says the destination of a read may
 * not be the buffer being read from, and OSMesa hands the caller that buffer
 * as ordinary memory.
 *
 * So: draw something whose colours are known, read it back as GL_RGB bytes
 * into the front of that same array, and check the bytes.  Then glFinish --
 * which is where a late mirror would land -- and check them again.
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
 * Three bytes per pixel, so the packing is unmistakably not the surface's --
 * if the mirror lands on this, every third byte moves.
 */
static int
check(const char *what, const unsigned char *p, int n)
{
    int i, bad = 0;

    for (i = 0; i < n; i++) {
        if (p[i * 3 + 0] != 0x00 || p[i * 3 + 1] != 0xFF ||
            p[i * 3 + 2] != 0x00)
            bad++;
    }
    printf("   %s: %d of %d pixels are not the green that was drawn\n",
           what, bad, n);
    return bad;
}

int
main(void)
{
    OSMesaContext ctx;
    unsigned long before, after;

    app = (unsigned long *)malloc((unsigned)(W * H) * sizeof(unsigned long));
    if (!app) { printf("no room\n"); return 2; }
    ctx = OSMesaCreateContext(OSMESA_ARGB, NULL);
    if (!ctx || !OSMesaMakeCurrent(ctx, app, GL_UNSIGNED_BYTE, W, H)) {
        printf("no context\n"); return 2;
    }

    printf("glReadPixels into the caller's own buffer\n\n");
    printf("   surface is the engine's : %s\n",
           (OSMGAMesaBufferOrigin() != 0UL) ? "yes" : "no");

    glViewport(0, 0, W, H);
    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    glOrtho(0.0, (double)W, 0.0, (double)H, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND); glDisable(GL_DITHER);
    glDisable(GL_TEXTURE_2D); glDisable(GL_CULL_FACE);
    glShadeModel(GL_FLAT);

    glClearColor(0.0f, 1.0f, 0.0f, 1.0f);       /* green everywhere */
    glClear(GL_COLOR_BUFFER_BIT);
    glFinish();

    /*
     * The read, aimed at the caller's own array.  A row of the surface is
     * 128 pixels; 128 RGB triples is 384 bytes, well inside it.
     */
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    before = OSMGAMesaHookMirrors();
    glReadPixels(0, 0, W, 1, GL_RGB, GL_UNSIGNED_BYTE, (void *)app);
    after = OSMGAMesaHookMirrors();

    say("the read did not cost a mirror", after == before);
    say("the bytes are the ones asked for",
        check("straight after the read", (const unsigned char *)app, W) == 0);

    /*
     * And the late one.  glFinish is where a mirror deferred to the next
     * flush would land, and it would land on these bytes.
     */
    glFinish();
    say("and a later glFinish leaves them alone",
        check("after glFinish", (const unsigned char *)app, W) == 0);

    printf("\n%s (%d failing)\n",
           failures ? "=== PROBLEM ===" : "=== nothing to report ===",
           failures);
    return failures ? 1 : 0;
}
