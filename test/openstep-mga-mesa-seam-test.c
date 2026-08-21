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
#define NSHAPE 5
#define MAXTRI 4
static const int ntri[NSHAPE] = { 2, 2, 2, 4, 4 };
/* each triangle as three indices into the shape's vertex list */
static const int tidx[NSHAPE][MAXTRI][3] = {
    { {0,1,2}, {0,2,3}, {0,0,0}, {0,0,0} },
    { {0,1,2}, {0,2,3}, {0,0,0}, {0,0,0} },
    { {0,1,2}, {0,2,3}, {0,0,0}, {0,0,0} },
    { {0,1,2}, {0,2,3}, {0,3,4}, {0,4,5} },          /* fan around vertex 0 */
    { {0,4,1}, {4,5,1}, {1,5,2}, {5,6,2} }           /* strip */
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
    {  40.0f, 120.0f, 200.0f, 280.0f,  48.0f, 128.0f, 208.0f, 288.0f }
};
static const float vy[NSHAPE][NVERT] = {
    {  40.0f,  40.0f, 180.0f, 180.0f },
    {  40.0f + OFF,  40.0f + OFF, 180.0f + OFF, 180.0f + OFF },
    {  72.0f, 132.0f, 172.0f,  48.0f },
    { 120.0f,  40.0f,  24.0f,  28.0f, 160.0f, 216.0f },     /* fan */
    {  60.0f,  52.0f,  64.0f,  56.0f, 180.0f, 188.0f, 176.0f, 184.0f }
};
static const int nvert[NSHAPE] = { 4, 4, 4, 6, 8 };

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
    int s = (shape[0] >= 'A' && shape[0] <= 'E') ? (shape[0] - 'A') : 0;
    int t;
    long x, y;
    unsigned long d0, s0, x0;

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
    x0 = OSMGAMesaHookDeclined();

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
    } else { printf("mode: soloN both rev plane zplane\n"); return 2; }
    glFinish();

    printf("# shape %c mode %s ntri %d\n", 'A' + s, mode, ntri[s]);
    for (x = 0; x < nvert[s]; x++)
        printf("# v%ld %.7f %.7f\n", x, vx[s][x], vy[s][x]);
    for (x = 0; x < ntri[s]; x++)
        printf("# t%ld %d %d %d\n", x, tidx[s][x][0], tidx[s][x][1], tidx[s][x][2]);
    printf("# counters drawn=%lu software=%lu declined=%lu\n",
           OSMGAMesaHookDrawn() - d0, OSMGAMesaHookSoftware() - s0,
           OSMGAMesaHookDeclined() - x0);
    printf("# mode %s\n",
           (OSMGAMesaBufferOrigin() == 0UL) ? "software"
           : ((OSMGAMesaHookSoftware() - s0 != 0UL) ? "MIXED" : "hardware"));
    if (strcmp(mode, "zplane") == 0) {
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
