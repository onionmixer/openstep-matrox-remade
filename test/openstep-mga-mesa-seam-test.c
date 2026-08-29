/*
 * openstep-mga-mesa-seam-test.c -- do neighbouring triangles meet cleanly?
 *
 * Everything measured so far has been one triangle at a time, and real
 * content is a mesh.  A triangle can be exactly right on its own and still
 * crack against the one beside it, or draw the shared edge twice.
 *
 * The oracle was checked before this leaned on it: the rule itself tiles a
 * diagonal-split quad with no gap and no overlap, so anything seen here is
 * ours.
 *
 * Each triangle is drawn ALONE as well as together.  Comparing the union of
 * the two against the rule is not enough -- one triangle under-covering while
 * the other over-covers leaves a perfect union -- so the proof is each
 * triangle against its own oracle mask, plus the intersection being empty.
 *
 * Shape C exists because of a hazard the first draft of this test would have
 * missed.  The back end picks a sub-pixel precision per triangle, so two
 * triangles sharing a vertex could quantise it differently -- and the four
 * quads first chosen all happened to pick the SAME precision, which would
 * have reported the hazard as absent when it was merely not exercised.  C is
 * convex, so its diagonal split tiles, and its two triangles pick 5 and 4.
 *
 *   cc -O -Wall -o /tmp/seam openstep-mga-mesa-seam-test.c \
 *      -I<mesa>/include -L<built> -lGL_mga
 *   /tmp/seam <shape A..C> <mode>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <GL/osmesa.h>

extern unsigned long OSMGAMesaHookDrawn(void);
extern unsigned long OSMGAMesaHookDeclined(void);
extern unsigned long OSMGAMesaHookSoftware(void);
extern unsigned long OSMGAMesaBufferOrigin(void);
extern unsigned long OSMGAMesaDepthClamps(void);
extern unsigned long OSMGAMesaHookHardState(void);
extern unsigned long OSMGAMesaHookSoftState(void);

#define W  320
#define H  240
#define CLEARC 0xFF102030UL

/*
 * A  integer quad, the regression control
 * B  the same offset by 37/128, so no sample lands on an edge
 * C  convex, and its two triangles pick different sub-pixel precisions
 *
 * The colours come from one affine function of position, c = x/4 + y/4 + 120,
 * so BOTH triangles interpolate the same plane and a visible seam is a defect
 * rather than a legitimate discontinuity.  The fourth colour is not free: it
 * is what the other three and the geometry require.
 */
/*
 * D and E are the cases a quad cannot reach.  D is a fan of four triangles
 * meeting at one vertex, with the sectors deliberately unequal so that three
 * different sub-pixel precisions are chosen -- 5, 6, 5 and 4 -- which is the
 * hardest test of whether the shared vertex lands in the same place for all
 * of them.  E is a strip, where edges are shared pairwise rather than at a
 * point.  Three triangles cannot share one whole edge in a plane, so these
 * two are what "more than two meet" actually means.
 */
/*
 * F is not a mesh.  It is two triangles that OVERLAP, which is what the
 * mixed-path ordering question needs: abutting triangles cannot say which
 * write landed last, and overlapping ones can.
 */
#define NSHAPE 6
#define MAXTRI 4
static const int ntri[NSHAPE] = { 2, 2, 2, 4, 4, 2 };
/* each triangle as three indices into the shape's vertex list */
static const int tidx[NSHAPE][MAXTRI][3] = {
    { {0,1,2}, {0,2,3}, {0,0,0}, {0,0,0} },
    { {0,1,2}, {0,2,3}, {0,0,0}, {0,0,0} },
    { {0,1,2}, {0,2,3}, {0,0,0}, {0,0,0} },
    { {0,1,2}, {0,2,3}, {0,3,4}, {0,4,5} },          /* fan around vertex 0 */
    { {0,4,1}, {4,5,1}, {1,5,2}, {5,6,2} },          /* strip */
    { {0,1,2}, {3,4,5}, {0,0,0}, {0,0,0} }           /* overlapping pair */
};
#define NVERT 8

#define OFF  0.2890625f                  /* 37/128 */
static const float qx[3][4] = {
    {  40.0f, 200.0f, 200.0f,  40.0f },
    {  40.0f + OFF, 200.0f + OFF, 200.0f + OFF, 40.0f + OFF },
    { 148.0f,  24.0f, 144.0f, 296.0f }
};
static const float qy[3][4] = {
    {  40.0f,  40.0f, 180.0f, 180.0f },
    {  40.0f + OFF,  40.0f + OFF, 180.0f + OFF, 180.0f + OFF },
    {  72.0f, 132.0f, 172.0f,  48.0f }
};

static const float vx[NSHAPE][NVERT] = {
    {  40.0f, 200.0f, 200.0f,  40.0f },
    {  40.0f + OFF, 200.0f + OFF, 200.0f + OFF, 40.0f + OFF },
    { 148.0f,  24.0f, 144.0f, 296.0f },
    { 160.0f,  60.0f, 176.0f, 188.0f, 304.0f,  64.0f },     /* fan */
    {  40.0f, 120.0f, 200.0f, 280.0f,  48.0f, 128.0f, 208.0f, 288.0f },
    {  50.0f, 260.0f,  90.0f, 240.0f,  70.0f, 250.0f }      /* overlap */
};
static const float vy[NSHAPE][NVERT] = {
    {  40.0f,  40.0f, 180.0f, 180.0f },
    {  40.0f + OFF,  40.0f + OFF, 180.0f + OFF, 180.0f + OFF },
    {  72.0f, 132.0f, 172.0f,  48.0f },
    { 120.0f,  40.0f,  24.0f,  28.0f, 160.0f, 216.0f },     /* fan */
    {  60.0f,  52.0f,  64.0f,  56.0f, 180.0f, 188.0f, 176.0f, 184.0f },
    {  50.0f,  70.0f, 200.0f,  50.0f,  90.0f, 210.0f }      /* overlap */
};
static const int nvert[NSHAPE] = { 4, 4, 4, 6, 8, 6 };

static unsigned long *app;

/*
 * Depth from the same kind of affine function, so both neighbours share one
 * depth plane and a seam in it is a defect.  The window depth wanted is
 * 20000 + 60*x + 40*y, and the calibration says a window code k comes from an
 * object depth of 1 - k/32767.5.
 */
static double
zfor(int s, int v)
{
    double code = 20000.5 + 60.37 * (double)vx[s][v] + 40.11 * (double)vy[s][v];

    return 1.0 - code / 32767.5;
}

/*
 * Force the next primitives to the software rasteriser without changing which
 * pixels they cover.
 *
 * A scissor over the whole surface filters nothing, and the chooser refuses
 * the state outright: gl_update_state sets SCISSOR_BIT in RasterMask
 * (Mesa state.c:814) and osmgaMesaChooseTriangle rejects every RasterMask bit
 * outside ALPHABUF/DEPTH/BLEND.  The same switch is already used by
 * openstep-mga-mesa-depth-agree.c:95.
 *
 * An all-ones polygon stipple would also work and was the first choice; it
 * puts an extra span stage in the software path, which is one more thing that
 * could differ, so it was dropped in favour of this.
 */
/*
 * The path, changed by asking for it.
 *
 * This used to be a full-surface scissor -- a state the chooser
 * refused, which clipped nothing -- and that was borrowed rather than
 * owned: the moment the scissor is admitted, this comparison would
 * become hardware against hardware and pass without asking anything.
 */
static void softOn(void)  { OSMGAMesaHookForceSoftware(1); }
static void softOff(void) { OSMGAMesaHookForceSoftware(0); }

/* One colour per triangle, so a pixel can be attributed to the triangle that
 * wrote it.  Abutting triangles never share a colour. */
static const unsigned char tcol[MAXTRI][3] = {
    { 255,   0,   0 }, {   0, 255,   0 },
    {   0,   0, 255 }, { 255, 255,   0 }
};

/* Non-clear pixels now in the application's buffer -- the bookkeeping
 * checkpoint.  Nothing about the counters proves a copy back happened; this
 * looks at the buffer the application would read. */
static long
painted(void)
{
    long n = 0, i;

    for (i = 0; i < (long)W * (long)H; i++)
        if (app[i] != CLEARC) n++;
    return n;
}

static void
tri(int s, int t, int plane, int r, int g, int b)
{
    int k;

    glBegin(GL_TRIANGLES);
      for (k = 0; k < 3; k++) {
          int v = tidx[s][t][k];

          if (plane == 2) {
              glColor4ub(200, 200, 200, 255);
              glVertex3d((double)vx[s][v], (double)vy[s][v], zfor(s, v));
              continue;
          }
          if (plane) {
              int c = (int)(vx[s][v] / 4.0f + vy[s][v] / 4.0f + 120.0f);

              glColor4ub((GLubyte)c, (GLubyte)c, (GLubyte)c, 255);
          } else {
              glColor4ub((GLubyte)r, (GLubyte)g, (GLubyte)b, 255);
          }
          glVertex3f(vx[s][v], vy[s][v], 0.0f);
      }
    glEnd();
}

int
main(int argc, char **argv)
{
    OSMesaContext ctx;
    const char *shape = (argc > 1) ? argv[1] : "A";
    const char *mode  = (argc > 2) ? argv[2] : "both";
    int s = (shape[0] >= 'A' && shape[0] <= 'F') ? (shape[0] - 'A') : 0;
    int t;
    long x, y;
    unsigned long d0, s0, x0, h0, w0, w1;

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
    glDisable(GL_CULL_FACE); glDisable(GL_TEXTURE_2D); glDisable(GL_ALPHA_TEST);
    glShadeModel(strcmp(mode, "plane") == 0 ? GL_SMOOTH : GL_FLAT);

    glClearColor(0x10/255.0f, 0x20/255.0f, 0x30/255.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    d0 = OSMGAMesaHookDrawn(); s0 = OSMGAMesaHookSoftware();
    w1 = OSMGAMesaHookWarp();
    x0 = OSMGAMesaHookDeclined();
    h0 = OSMGAMesaHookHardState(); w0 = OSMGAMesaHookSoftState();

    if (strncmp(mode, "solo", 4) == 0) {
        t = atoi(mode + 4) - 1;
        if (t < 0 || t >= ntri[s]) { printf("no such triangle\n"); return 2; }
        tri(s, t, 0, 0, 255, 0);
    } else if (strcmp(mode, "both") == 0) {
        for (t = 0; t < ntri[s]; t++)
            tri(s, t, 0, (t & 1) ? 255 : 0, (t & 1) ? 0 : 255, 0);
    } else if (strcmp(mode, "rev") == 0) {
        for (t = ntri[s] - 1; t >= 0; t--)
            tri(s, t, 0, (t & 1) ? 255 : 0, (t & 1) ? 0 : 255, 0);
    } else if (strcmp(mode, "plane") == 0) {
        for (t = 0; t < ntri[s]; t++) tri(s, t, 1, 0, 0, 0);
    } else if (strcmp(mode, "zplane") == 0) {
        /* GL_LESS against a far clear rejects nothing, so the depth value is
         * measured rather than the depth test. */
        glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS); glDepthMask(GL_TRUE);
        glClearDepth(1.0); glClear(GL_DEPTH_BUFFER_BIT);
        for (t = 0; t < ntri[s]; t++) tri(s, t, 2, 0, 0, 0);
    } else if (strncmp(mode, "ssolo", 5) == 0) {
        /*
         * One triangle, alone, on the software path and into the substituted
         * surface.  This is the only honest baseline for a software triangle
         * in a mixed frame: in a frame where its neighbour also draws, the
         * neighbour overwrites whatever they both claim, so a mask taken from
         * a two-triangle run is short by exactly the disputed pixels and
         * would charge those to the mixing.
         */
        t = atoi(mode + 5) - 1;
        if (t < 0 || t >= ntri[s]) { printf("no such triangle\n"); return 2; }
        softOn();
        tri(s, t, 0, tcol[t][0], tcol[t][1], tcol[t][2]);
        softOff();
    } else if (strcmp(mode, "hsolo1") == 0 || strcmp(mode, "hsolo2") == 0 ||
               strcmp(mode, "hsolo3") == 0 || strcmp(mode, "hsolo4") == 0) {
        t = mode[5] - '1';
        if (t < 0 || t >= ntri[s]) { printf("no such triangle\n"); return 2; }
        tri(s, t, 0, tcol[t][0], tcol[t][1], tcol[t][2]);
    } else if (strcmp(mode, "allsoft") == 0) {
        /*
         * The control.  Acceleration stays ON -- OSMGA_MESA_ACCEL=0 would make
         * the probe refuse (Probe.c:101) and the VRAM substitution would never
         * happen (Buffer.c:370), so it measures a different surface than the
         * one the mixed frame draws into.
         */
        softOn();
        for (t = 0; t < ntri[s]; t++)
            tri(s, t, 0, tcol[t][0], tcol[t][1], tcol[t][2]);
        softOff();
    } else if (strcmp(mode, "mix") == 0 || strcmp(mode, "mixrev") == 0) {
        int softFirst = (strcmp(mode, "mixrev") == 0);

        for (t = 0; t < ntri[s]; t++) {
            int soft = softFirst ? (t == 0) : (t != 0);

            if (soft) softOn(); else softOff();
            tri(s, t, 0, tcol[t][0], tcol[t][1], tcol[t][2]);
            /*
             * Checkpoint after each triangle: flush, then look at the
             * application's buffer.  The counters cannot prove the copy back
             * -- Clear and RenderStart mark the surface dirty unconditionally
             * (Hook.c:503, :528), so the mirror runs whether or not either
             * path did its own bookkeeping.  This is what can tell.
             */
            glFlush();
            printf("# checkpoint %d %s painted %ld\n",
                   t, soft ? "sw" : "hw", painted());
        }
        softOff();
    } else if (strcmp(mode, "over") == 0 || strcmp(mode, "overrev") == 0) {
        /*
         * Two overlapping triangles, one per path.  The later one must own
         * every pixel of the overlap.
         *
         * "over" (hardware first) cannot fail on ordering: the submit ioctl
         * waits for DMA completion and then for the engine to go idle
         * (OpenStepMGAReplacementDisplay.m:3727, :3734), so the engine is
         * finished before the CPU writes.  It is kept as the control.
         * "overrev" is the live one -- the fence waits for the engine, it
         * does not push the CPU's dirty cache lines out, so a line written by
         * the software triangle can be evicted over the engine's result.
         */
        int softFirst = (strcmp(mode, "overrev") == 0);

        for (t = 0; t < ntri[s]; t++) {
            int soft = softFirst ? (t == 0) : (t != 0);

            if (soft) softOn(); else softOff();
            tri(s, t, 0, tcol[t][0], tcol[t][1], tcol[t][2]);
            glFlush();
            printf("# checkpoint %d %s painted %ld\n",
                   t, soft ? "sw" : "hw", painted());
        }
        softOff();
    } else if (strcmp(mode, "mixz") == 0) {
        /* Mixed paths sharing one depth plane.  Colour identifies the writer,
         * the depth buffer is what is judged. */
        glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS); glDepthMask(GL_TRUE);
        glClearDepth(1.0); glClear(GL_DEPTH_BUFFER_BIT);
        for (t = 0; t < ntri[s]; t++) {
            if (t != 0) softOn(); else softOff();
            tri(s, t, 2, 0, 0, 0);
        }
        softOff();
    } else {
        printf("mode: soloN ssoloN hsoloN both rev plane zplane "
               "allsoft mix mixrev over overrev mixz\n");
        return 2;
    }
    glFinish();

    printf("# shape %c mode %s ntri %d\n", 'A' + s, mode, ntri[s]);
    printf("# select hard %lu soft %lu\n",
           OSMGAMesaHookHardState() - h0, OSMGAMesaHookSoftState() - w0);
    for (x = 0; x < nvert[s]; x++)
        printf("# v%ld %.7f %.7f\n", x, vx[s][x], vy[s][x]);
    for (x = 0; x < ntri[s]; x++)
        printf("# t%ld %d %d %d\n", x, tidx[s][x][0], tidx[s][x][1], tidx[s][x][2]);
    /*
     * warp is a SUBSET of drawn.  Both accelerated tiers are "the engine
     * drew it", so drawn alone cannot tell a WARP run from one that asked
     * for WARP and quietly got trapezoids, and this file's whole subject
     * -- who owns the pixels along a shared edge -- is a per-tier answer.
     */
    printf("# counters drawn=%lu software=%lu declined=%lu warp=%lu\n",
           OSMGAMesaHookDrawn() - d0, OSMGAMesaHookSoftware() - s0,
           OSMGAMesaHookDeclined() - x0, OSMGAMesaHookWarp() - w1);
    printf("# mode %s\n",
           (OSMGAMesaBufferOrigin() == 0UL) ? "software"
           : ((OSMGAMesaHookSoftware() - s0 != 0UL) ? "MIXED" : "hardware"));
    if (strcmp(mode, "zplane") == 0 || strcmp(mode, "mixz") == 0) {
        void *zb; GLint dw, dh, bpv;

        printf("# depth starts clamped: %lu\n", OSMGAMesaDepthClamps());
        if (!OSMesaGetDepthBuffer(ctx, &dw, &dh, &bpv, &zb) || bpv != 2) {
            printf("# no 16-bit depth buffer\n"); return 2;
        }
        for (y = 0; y < H; y++)
            for (x = 0; x < W; x++) {
                if (app[y * W + x] == CLEARC) continue;
                printf("Z %ld %ld %u\n", x, y,
                       (unsigned)((unsigned short *)zb)[y * dw + x]);
            }
    } else {
        for (y = 0; y < H; y++)
            for (x = 0; x < W; x++) {
                unsigned long c = app[y * W + x];

                if (c == CLEARC) continue;
                printf("P %ld %ld %lu %lu %lu\n", x, y,
                       (c >> 16) & 0xFFUL, (c >> 8) & 0xFFUL, c & 0xFFUL);
            }
    }
    OSMesaDestroyContext(ctx);
    return 0;
}
