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

static unsigned long *app;

static void
tri(int s, int i0, int i1, int i2, int plane, int r, int g, int b)
{
    int idx[3], k;

    idx[0] = i0; idx[1] = i1; idx[2] = i2;
    glBegin(GL_TRIANGLES);
      for (k = 0; k < 3; k++) {
          if (plane) {
              int c = (int)(qx[s][idx[k]] / 4.0f + qy[s][idx[k]] / 4.0f + 120.0f);

              glColor4ub((GLubyte)c, (GLubyte)c, (GLubyte)c, 255);
          } else {
              glColor4ub((GLubyte)r, (GLubyte)g, (GLubyte)b, 255);
          }
          glVertex3f(qx[s][idx[k]], qy[s][idx[k]], 0.0f);
      }
    glEnd();
}

int
main(int argc, char **argv)
{
    OSMesaContext ctx;
    const char *shape = (argc > 1) ? argv[1] : "A";
    const char *mode  = (argc > 2) ? argv[2] : "both";
    int s = (shape[0] >= 'A' && shape[0] <= 'C') ? (shape[0] - 'A') : 0;
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

    /* the quad is v0 v1 v2 v3; the diagonal is v0-v2 */
    if (strcmp(mode, "solo1") == 0)        tri(s, 0, 1, 2, 0, 0, 255, 0);
    else if (strcmp(mode, "solo2") == 0)   tri(s, 0, 2, 3, 0, 0, 255, 0);
    else if (strcmp(mode, "both") == 0)  { tri(s, 0, 1, 2, 0, 0, 255, 0);
                                           tri(s, 0, 2, 3, 0, 255, 0, 0); }
    else if (strcmp(mode, "rev") == 0)   { tri(s, 0, 2, 3, 0, 255, 0, 0);
                                           tri(s, 0, 1, 2, 0, 0, 255, 0); }
    else if (strcmp(mode, "plane") == 0) { tri(s, 0, 1, 2, 1, 0, 0, 0);
                                           tri(s, 0, 2, 3, 1, 0, 0, 0); }
    else { printf("mode: solo1 solo2 both rev plane\n"); return 2; }
    glFinish();

    printf("# shape %c mode %s\n", 'A' + s, mode);
    for (x = 0; x < 4; x++)
        printf("# v%ld %.7f %.7f\n", x, qx[s][x], qy[s][x]);
    printf("# counters drawn=%lu software=%lu declined=%lu\n",
           OSMGAMesaHookDrawn() - d0, OSMGAMesaHookSoftware() - s0,
           OSMGAMesaHookDeclined() - x0);
    printf("# mode %s\n",
           (OSMGAMesaBufferOrigin() == 0UL) ? "software"
           : ((OSMGAMesaHookSoftware() - s0 != 0UL) ? "MIXED" : "hardware"));
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++) {
            unsigned long c = app[y * W + x];

            if (c == CLEARC) continue;
            printf("P %ld %ld %lu %lu %lu\n", x, y,
                   (c >> 16) & 0xFFUL, (c >> 8) & 0xFFUL, c & 0xFFUL);
        }
    OSMesaDestroyContext(ctx);
    return 0;
}
