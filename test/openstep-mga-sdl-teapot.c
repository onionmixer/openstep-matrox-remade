/*
 * The Utah teapot through SDL2, to find out what SDL2's delivery costs.
 *
 * ONE SOURCE, TWO BINARIES, as the other demo pairs in this workspace are:
 *
 *   sdlteapot_sw       stock libGL.a  + libSDL2.a
 *   sdlteapot_hybrid   libGL_mga.a    + libSDL2.a
 *
 * The SDL2 port's GL backend calls OSMesaCreateContext(OSMESA_ARGB, ...) and
 * OSMesaMakeCurrent, which are exactly where this driver substitutes a video
 * memory surface -- so the hybrid binary needs NO change to SDL2 for the card
 * to draw.  Linking one archive instead of the other is the whole difference.
 *
 * WHAT THIS IS FOR, AND IT IS NOT SPEED.  SDL2's OPENSTEP_GL_SwapWindow pushes
 * the OSMesa buffer up through AppKit, so with the driver engaged every frame
 * becomes: the card draws into video memory, the driver reads it back into the
 * caller's array, AppKit puts it on the screen.  The sibling window demo in
 * this project reaches 52 fps by NOT doing that -- it presents with the
 * driver's video-memory-to-video-memory blit, 3.69 ms against AppKit's 62.79.
 * The readback was estimated at about 358 ms for 800x600 from an earlier
 * table.  This demo exists to replace that estimate with a measurement, and
 * to say plainly which path each frame took.
 *
 * SO THE OUTPUT IS EVIDENCE, NOT A SCORE.  "It ran the hybrid binary" proves
 * nothing: the driver falls back whenever the device, the mode, the mapping
 * or the surface admission is unavailable, and a fallback still draws a
 * correct teapot at a software speed.  A hardware frame needs the surface
 * claimed, triangles drawn by the card, and batches submitted; and to say
 * WARP drew it you need warp == drawn as well, because trapezoids at nought
 * also describes a total software fallback.
 *
 * THE WINDOW DOES NOT RESIZE.  The driver owns one surface at one size and
 * refuses a different one -- correctly, and silently, by letting the caller
 * render in its own memory.  A resized run would put hardware frames and
 * software frames under a single frame rate.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <SDL.h>
#include <GL/gl.h>

#ifdef OSMGA_SDLTEAPOT_PLAIN
#define OSMGA_SDLTEAPOT_ACCEL 0
#define OSMGAMesaBufferOrigin()   0UL
#define OSMGAMesaBufferCopies()   0UL
#define OSMGAMesaHookDrawn()      0UL
#define OSMGAMesaHookWarp()       0UL
#define OSMGAMesaHookTraps()      0UL
#define OSMGAMesaHookBatches()    0UL
#define OSMGAMesaHookSoftware()   0UL
#define OSMGAMesaHookUnsupported() 0UL
#define OSMGAMesaHookReplayed()   0UL
#define OSMGAMesaHookMirrors()    0UL
#else
#define OSMGA_SDLTEAPOT_ACCEL 1
#include "../mesa/OpenStepMGAMesaHook.h"
#include "../mesa/OpenStepMGAMesaBuffer.h"
#include "../mesa/OpenStepMGAMesaProbe.h"
#endif

/* patchdata, cpdata and teapot(), cut from the Mesa tree at build time */
#include "teapot-geometry.h"

/* Settable so a run can ask whether a refusal is about the size.  The
 * driver owns ONE surface at ONE size and refuses any other. */
static int W = 800;
static int H = 600;
#define WARMUP 20
#define FRAMES 200

static double
now(void)
{
    struct timeval t;
    gettimeofday(&t, (struct timezone *)0);
    return (double)t.tv_sec + (double)t.tv_usec / 1e6;
}

/*
 * The same scene as this project's own window demo, value for value, so the
 * two sets of numbers can be put beside each other and mean something.  The
 * frustum's top and bottom are swapped there because a blit cannot flip a
 * row order; it is kept here so the picture matches, and culling stays off
 * with it because the winding reverses.
 */
static void
setupScene(void)
{
    GLfloat amb[4], dif[4], pos[4], lamb[4];
    GLfloat mamb[4], mdif[4], mspec[4];

    glViewport(0, 0, W, H);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    /*
     * A NORMAL frustum, unlike this project's own window demo.
     *
     * That one swaps top and bottom because its delivery is a blit that
     * cannot reverse a row order, so the projection does it instead -- and
     * it turns culling off to match, since the winding reverses with it.
     * SDL2 does not need that: its backend calls
     * OSMesaPixelStore(OSMESA_Y_UP, 0) and gets top-down rows already.
     * Copying the swap as well as the scene put the teapot upside down,
     * which is what the operator saw.
     */
    glFrustum(-1.0, 1.0, -0.75, 0.75, 2.0, 20.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(0.0f, -0.2f, -6.0f);
    glRotatef(-20.0f, 1.0f, 0.0f, 0.0f);

    glDisable(GL_BLEND); glDisable(GL_DITHER);
    glDisable(GL_TEXTURE_2D); glDisable(GL_CULL_FACE);
    glShadeModel(GL_SMOOTH);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glClearDepth(1.0);

    amb[0] = 0.0f; amb[1] = 0.0f; amb[2] = 0.0f; amb[3] = 1.0f;
    dif[0] = 1.0f; dif[1] = 1.0f; dif[2] = 1.0f; dif[3] = 1.0f;
    pos[0] = 0.0f; pos[1] = 3.0f; pos[2] = 3.0f; pos[3] = 0.0f;
    lamb[0] = 0.2f; lamb[1] = 0.2f; lamb[2] = 0.2f; lamb[3] = 1.0f;
    glLightfv(GL_LIGHT0, GL_AMBIENT, amb);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, dif);
    glLightfv(GL_LIGHT0, GL_POSITION, pos);
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, lamb);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);

    mamb[0] = 0.18f; mamb[1] = 0.07f; mamb[2] = 0.03f; mamb[3] = 1.0f;
    mdif[0] = 0.9f;  mdif[1] = 0.35f; mdif[2] = 0.15f; mdif[3] = 1.0f;
    mspec[0] = 0.9f; mspec[1] = 0.9f; mspec[2] = 0.9f; mspec[3] = 1.0f;
    glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT, mamb);
    glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, mdif);
    glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, mspec);
    glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, 50.0f);

    glClearColor(0.06f, 0.08f, 0.14f, 1.0f);
}

static void
drawFrame(double angle)
{
    glClear((GLbitfield)(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT));
    glPushMatrix();
    glRotatef((float)(angle * 57.29578), 0.0f, 1.0f, 0.0f);
    teapot(4, 1.0, GL_FILL);
    glPopMatrix();
}

int
main(int argc, char **argv)
{
    SDL_Window *win;
    SDL_GLContext ctx;
    SDL_Event ev;
    double angle = 0.0, t0, t1, t2;
    double renderMs = 0.0, swapMs = 0.0, wall0, wall1;
    unsigned long d0, w0, p0, b0, s0, u0, r0, m0, c0;
    int i, quit = 0, resized = 0;

    if (argc >= 3) { W = atoi(argv[1]); H = atoi(argv[2]); }
    if (W < 64 || H < 64) { printf("usage: %s [width height]\n", argv[0]); return 2; }
#if OSMGA_SDLTEAPOT_ACCEL
#define WHERE(what) printf("   %-24s: %s\n", what, \
        OSMGAMesaBufferOrigin() ? "the engine's" : "the caller's")
#else
#define WHERE(what) ((void)0)
#endif
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        printf("SDL_Init: %s\n", SDL_GetError());
        return 2;
    }
    /*
     * Not resizable, and no accelerated-visual request.  The first because
     * the driver's surface is one size; the second because it buys nothing
     * here -- what decides acceleration is which libGL was linked, not an
     * attribute.
     */
    win = SDL_CreateWindow("SDL2 teapot", SDL_WINDOWPOS_CENTERED,
                           SDL_WINDOWPOS_CENTERED, W, H, SDL_WINDOW_OPENGL);
    if (!win) { printf("SDL_CreateWindow: %s\n", SDL_GetError()); return 2; }
    WHERE("after CreateWindow");
    ctx = SDL_GL_CreateContext(win);
    if (!ctx) { printf("SDL_GL_CreateContext: %s\n", SDL_GetError()); return 2; }

    /*
     * Asked here as well as at the end.  A surface claimed at bind time and
     * gone by the last frame means something rebound; only one of the two
     * answers would say so.
     */
    WHERE("after CreateContext");

    setupScene();

    /* Warm up before anything is counted: the first frames pay for the
     * surface claim, the first batch buffer and whatever AppKit does once. */
    for (i = 0; i < WARMUP; i++) {
        drawFrame(angle); angle += 0.05;
        SDL_GL_SwapWindow(win);
        while (SDL_PollEvent(&ev)) { if (ev.type == SDL_QUIT) quit = 1; }
    }

    d0 = OSMGAMesaHookDrawn();    w0 = OSMGAMesaHookWarp();
    p0 = OSMGAMesaHookTraps();    b0 = OSMGAMesaHookBatches();
    s0 = OSMGAMesaHookSoftware(); u0 = OSMGAMesaHookUnsupported();
    r0 = OSMGAMesaHookReplayed(); m0 = OSMGAMesaHookMirrors();
    c0 = OSMGAMesaBufferCopies();

    wall0 = now();
    for (i = 0; i < FRAMES && !quit; i++) {
        t0 = now();
        drawFrame(angle); angle += 0.05;
        glFinish();
        t1 = now();
        SDL_GL_SwapWindow(win);
        t2 = now();
        renderMs += (t1 - t0) * 1000.0;
        swapMs   += (t2 - t1) * 1000.0;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) quit = 1;
            if (ev.type == SDL_WINDOWEVENT &&
                ev.window.event == SDL_WINDOWEVENT_RESIZED) resized = 1;
        }
    }
    wall1 = now();

    printf("\nthe Utah teapot through SDL2, %dx%d, %d frames\n\n", W, H, i);
    printf("   build                   : %s\n",
           OSMGA_SDLTEAPOT_ACCEL ? "libGL_mga.a (the driver may accelerate)"
                                 : "stock libGL.a (software only)");
#if OSMGA_SDLTEAPOT_ACCEL
    {
        const char *v = getenv("OSMGA_MESA_WARP");
        printf("   OSMGA_MESA_WARP         : %s\n",
               (v && *v) ? v : "unset -- the Configure setting decides");
    }
    /*
     * "It ran the hybrid binary" is not evidence.  These four lines are.
     */
    /*
     * "NO" on its own sends the reader looking through the driver's source
     * for which of a dozen refusals it was.  The probe's verdict says.
     */
    {
        OSMGAMesaProbe probe;

        OSMGAMesaProbeRun(&probe);
        printf("   surface is the engine's : %s\n",
               OSMGAMesaBufferOrigin() ? "yes"
                                       : "NO -- everything below is software");
        printf("   the probe says          : %s\n",
               OSMGAMesaProbeVerdictString(probe.verdict));
        if (probe.missing != 0UL)
            printf("   capabilities missing    : 0x%lx\n", probe.missing);
    }
    printf("   drawn by the card       : %lu   (batches %lu)\n",
           OSMGAMesaHookDrawn() - d0, OSMGAMesaHookBatches() - b0);
    printf("   of those, WARP took     : %lu   (trapezoids %lu)\n",
           OSMGAMesaHookWarp() - w0, OSMGAMesaHookTraps() - p0);
    printf("   left to Mesa / refused  : %lu / %lu   (replayed %lu)\n",
           OSMGAMesaHookSoftware() - s0, OSMGAMesaHookUnsupported() - u0,
           OSMGAMesaHookReplayed() - r0);
    /*
     * The readback, and it is the COPY count that says so.  The mirror
     * counter counts brackets that asked; some of them are turned away.
     */
    printf("   surface read back       : %lu copies   (%lu mirror calls)\n",
           OSMGAMesaBufferCopies() - c0, OSMGAMesaHookMirrors() - m0);
#endif
    printf("\n   where a frame goes, in milliseconds\n");
    printf("      render + finish      : %8.2f\n", renderMs / (double)i);
    printf("      SDL swap (AppKit)    : %8.2f\n", swapMs / (double)i);
    printf("      wall                 : %8.2f   (%.2f fps)\n",
           (wall1 - wall0) * 1000.0 / (double)i,
           (double)i / (wall1 - wall0));
    if (resized)
        printf("\n   THE WINDOW WAS RESIZED -- the driver refuses a surface of a\n"
               "   different size and lets the caller render in its own memory,\n"
               "   so these numbers mix hardware and software frames.  Discard\n"
               "   this run.\n");

    SDL_GL_DeleteContext(ctx);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
