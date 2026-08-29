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
#if !defined(OSMGA_SDLTEAPOT_PLAIN)
#include "SDL_openstepglpresent.h"
#endif

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
/* By bare name, as the offline teapot does: the shipped form finds these in
 * the installed prefix's Headers, and the in-tree build passes -I../mesa. */
#include "OpenStepMGAMesaHook.h"
#include "OpenStepMGAMesaBuffer.h"
#include "OpenStepMGAMesaProbe.h"
#endif

/* patchdata, cpdata and teapot(), cut from the Mesa tree at build time */
#include "teapot-geometry.h"

/* Settable so a run can ask whether a refusal is about the size.  The
 * driver owns ONE surface at ONE size and refuses any other. */
static int W = 800;
static int H = 600;
#define WARMUP 20
/* 200 frames is a measurement, not a viewing.  At 55 fps that is under
 * four seconds -- long enough for a number and too short for an eye, so
 * the count opens up for someone who wants to watch it turn. */
static int FRAMES = 200;

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
/*
 * WHICH DELIVERY.  0 = SDL's swap, which walks the surface back into the
 * caller's array and pushes that through AppKit.  1 = the driver's own
 * VRAM-to-VRAM stamp, which never crosses the bus.
 *
 * One binary measures both because the comparison is the experiment; and it
 * is a run-time choice rather than a build flag so the two numbers come from
 * the same compiled scene.
 */
static int presentMode;

/*
 * The stamp CANNOT REVERSE ROWS, so the projection does -- exactly as the
 * window demo in this suite does, and for the same reason.  SDL's swap can
 * reverse and does, so it wants the ordinary one.  Getting this wrong is
 * invisible in a frame rate and obvious on a screen.
 */
static void
projection(void)
{
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    if (presentMode == 1)
        glFrustum(-1.0, 1.0, 0.75, -0.75, 2.0, 20.0);   /* the demo flips */
    else
        /* Mode 2 reverses the rows itself, and SDL's swap reverses too, so
         * both want the ordinary projection.  Only the single blit does not. */
        glFrustum(-1.0, 1.0, -0.75, 0.75, 2.0, 20.0);
    glMatrixMode(GL_MODELVIEW);
}

static void
setupScene(void)
{
    GLfloat amb[4], dif[4], pos[4], lamb[4];
    GLfloat mamb[4], mdif[4], mspec[4];

    glViewport(0, 0, W, H);
    projection();
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
    /*
     * A half turn about x, and it is the MODEL that needs it.
     *
     * The teapot() lifted from tea.c carries its own two rotations, chosen
     * for that demo's coordinate system, and under this camera they leave
     * the pot standing on its lid -- the offline teapot in this suite
     * records the same thing, and uses the same normal frustum.
     *
     * Leaving it out is what the operator saw, and it was NOT the delivery:
     * openstep-sdl-gl-orientation reports the same bitmap layout from the
     * 2D path and the GL path, software and accelerated alike, so neither
     * SDL nor this driver flips anything.
     */
    glRotatef(180.0f, 1.0f, 0.0f, 0.0f);
    glRotatef((float)(angle * 57.29578), 0.0f, 1.0f, 0.0f);
    teapot(4, 1.0, GL_FILL);
    glPopMatrix();
}

#if OSMGA_SDLTEAPOT_ACCEL
/*
 * Stamp the surface onto the screen where the window is.
 *
 * A PRESENT IS NOT WINDOW COMPOSITING.  The server does not know these
 * pixels exist, so a stamp lands on top of whatever overlaps the rectangle:
 * every guard below exists to keep it from landing somewhere it should not.
 * The window demo in this suite reaches into AppKit for the same guards;
 * this one has only SDL, and says plainly where that is weaker.
 *
 * Returns 0 if it presented, 1 if it deliberately stood down, -1 on refusal.
 */
static int
stamp(SDL_Window *win, int screenH, int *lastX, int *lastY,
      unsigned long *outVerdict)
{
    int wx = 0, wy = 0;
    long dstX, dstY;
    long pw = (long)W, ph = (long)H;
    unsigned long srcX = 0UL, srcY = 0UL;
    Uint32 flags = SDL_GetWindowFlags(win);

    /* Not while hidden or minimised: the rectangle is somebody else's. */
    if ((flags & SDL_WINDOW_MINIMIZED) || !(flags & SDL_WINDOW_SHOWN))
        return 1;
    /*
     * And not while another application has the user.  True z-order is
     * unenforceable here, but raising another window requires clicking it,
     * which deactivates this one -- so stamping only while focused makes
     * occlusion look like occlusion.
     */
    if (!(flags & SDL_WINDOW_INPUT_FOCUS))
        return 1;

    SDL_GetWindowPosition(win, &wx, &wy);
    /*
     * MOVED SINCE THE LAST FRAME: stand down for one frame.
     *
     * This is the weaker guard.  SDL's position is a cache updated when a
     * move event is dispatched, while the window demo asks the server
     * directly -- so a drag can still leave a stamp at the position being
     * vacated for as long as the cache lags.  Named rather than hidden.
     */
    if (wx != *lastX || wy != *lastY) {
        *lastX = wx; *lastY = wy;
        return 1;
    }

    /*
     * SDL's window position already counts from the TOP of the screen --
     * the backend converts to AppKit's bottom-left origin when it places
     * the window -- so the scanout destination needs no flip.
     */
    dstX = (long)wx;
    dstY = (long)wy;

    /* Off the edges: push the source in and shorten, rather than ask the
     * kernel to refuse the whole thing. */
    if (dstX < 0) { srcX = (unsigned long)(-dstX); pw += dstX; dstX = 0; }
    if (dstY < 0) { srcY = (unsigned long)(-dstY); ph += dstY; dstY = 0; }
    if (dstY + ph > (long)screenH) ph = (long)screenH - dstY;
    if (pw <= 0 || ph <= 0)
        return 1;

    /*
     * MODE 2: the same rectangle, one row at a time, in reverse.
     *
     * The stamp cannot flip rows and a LIBRARY cannot flip the projection --
     * the projection belongs to the application -- so if SDL2 is ever to do
     * this by itself, the flip has to happen somewhere else.  Row by row is
     * the only place that needs no driver change, and this arm exists to
     * find out what it costs: a kernel entry per row rather than per frame.
     *
     * Nobody in this project has measured the cost of one kernel entry, so
     * the number is not guessed at here.
     */
    if (presentMode == 2) {
        long r;

        for (r = 0; r < ph; r++) {
            if (OSMGAMesaBufferPresentRect(srcX, srcY + (unsigned long)r,
                                           (unsigned long)pw, 1UL,
                                           dstX, dstY + (ph - 1 - r),
                                           outVerdict) != 0)
                return -1;
        }
        return 0;
    }

    if (OSMGAMesaBufferPresentRect(srcX, srcY, (unsigned long)pw,
                                   (unsigned long)ph, dstX, dstY,
                                   outVerdict) != 0)
        return -1;
    return 0;
}
#endif /* OSMGA_SDLTEAPOT_ACCEL */

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
    int screenH = 0, lastX = -1, lastY = -1;
    unsigned long stampSkips = 0UL, verdict = 0UL;
    int stampRefused = 0;
    double presentMs = 0.0;

    if (argc >= 3) { W = atoi(argv[1]); H = atoi(argv[2]); }
    {
        const char *v = getenv("OSMGA_SDLTEAPOT_FRAMES");
        if (v != 0 && *v != '\0') {
            int n = atoi(v);
            if (n > 0) FRAMES = n;
        }
    }
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

#if OSMGA_SDLTEAPOT_ACCEL
    {
        const char *v = getenv("OSMGA_SDLTEAPOT_PRESENT");
        SDL_DisplayMode dm;

        /*
         * Only with a surface to stamp.  Present mode declares the caller's
         * array stale; asking for that when the picture lives in the
         * caller's array would deliver nothing at all.
         */
        if (v != 0 && OSMGAMesaBufferOrigin()) {
            if (*v == '3')
                presentMode = 3;   /* SDL2 does it -- see below */
            else if (*v == '2')
                presentMode = 2;   /* this demo does it, row by row */
            else if (*v == '1' || *v == 'y' || *v == 'Y')
                presentMode = 1;   /* one blit, this demo flips its scene */
            /*
             * MODE 3 REGISTERS AND THEN FORGETS.
             *
             * The demo hands SDL2 the three driver functions and goes back
             * to an ordinary SDL_GL_SwapWindow loop -- no present mode of
             * its own, no rectangle arithmetic, no guards.  That is the
             * whole claim being tested: that an application needs to know
             * nothing beyond the registration.
             *
             * The struct is static on purpose.  SDL keeps the pointer, not
             * a copy, so a struct on the stack would be a dangling one by
             * the first swap.
             */
            if (presentMode == 3) {
                static const SDL_OpenStepGLPresent hooks = {
                    SDL_OPENSTEP_GLPRESENT_ABI,
                    sizeof(SDL_OpenStepGLPresent),
                    OSMGAMesaBufferOrigin,     /* signatures match exactly */
                    OSMGAMesaBufferPresentMode,
                    OSMGAMesaBufferPresentRect
                };
                SDL_SetWindowData(win, SDL_OPENSTEP_GLPRESENT_KEY,
                                  (void *)&hooks);
            } else if (presentMode) {
                OSMGAMesaBufferPresentMode(1);
            }
        }
        if (SDL_GetDesktopDisplayMode(0, &dm) == 0)
            screenH = dm.h;
        else
            presentMode = 0;      /* no screen height, no clipping, no stamp */
    }
#endif

    setupScene();

#if OSMGA_SDLTEAPOT_ACCEL
#define DELIVER()                                                          \
    do {                                                                   \
        if (presentMode == 3) {                                            \
            SDL_GL_SwapWindow(win);                                        \
        } else if (presentMode) {                                          \
            int rc = stamp(win, screenH, &lastX, &lastY, &verdict);        \
            if (rc > 0) stampSkips++;                                      \
            else if (rc < 0) { stampRefused = 1; quit = 1; }               \
        } else {                                                           \
            SDL_GL_SwapWindow(win);                                        \
        }                                                                  \
    } while (0)
#else
#define DELIVER() SDL_GL_SwapWindow(win)
#endif

    /* Warm up before anything is counted: the first frames pay for the
     * surface claim, the first batch buffer and whatever AppKit does once. */
    for (i = 0; i < WARMUP; i++) {
        drawFrame(angle); angle += 0.05;
        glFinish();
        DELIVER();
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
        DELIVER();
        t2 = now();
        renderMs += (t1 - t0) * 1000.0;
        if (presentMode) presentMs += (t2 - t1) * 1000.0;
        else             swapMs   += (t2 - t1) * 1000.0;
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
        printf("   delivery                : %s\n",
               presentMode == 3
                   ? "SDL2's own VRAM stamp (registered hooks)"
                   : presentMode == 2
                   ? "VRAM stamp, one row at a time (reversed)"
                   : presentMode
                       ? "the driver's VRAM stamp (no readback)"
                       : "SDL swap -- the surface is read back every frame");
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
#if OSMGA_SDLTEAPOT_ACCEL
    if (presentMode) {
        printf("      VRAM present (stamp) : %8.2f\n", presentMs / (double)i);
        printf("      frames stood down    : %lu   (moved, hidden or "
               "unfocused)\n", stampSkips);
        if (stampRefused)
            printf("      PRESENT REFUSED      : verdict %lu -- the run "
                   "stopped there\n", verdict);
    } else
#endif
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
