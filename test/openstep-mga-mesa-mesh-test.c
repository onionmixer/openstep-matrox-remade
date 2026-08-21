/*
 * A long mesh, drawn once, judged per triangle.
 *
 * Four triangles could be judged by drawing each one alone.  A hundred and
 * twenty eight cannot -- that is a hundred and twenty eight runs.  So the mesh
 * arrives already coloured by test/mesh-gen.py, which gives no two triangles
 * that share even a VERTEX the same colour.  A pixel then names its writer in
 * a single run, and a pixel one triangle took from a neighbour is visible.
 *
 * The mesh is read at run time rather than compiled in, so the judge and the
 * drawing use one description and cannot drift apart.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <GL/osmesa.h>

extern unsigned long OSMGAMesaHookDrawn(void);
extern unsigned long OSMGAMesaHookDeclined(void);
extern unsigned long OSMGAMesaHookSoftware(void);
extern unsigned long OSMGAMesaHookHardState(void);
extern unsigned long OSMGAMesaHookSoftState(void);

#define W  320
#define H  240
#define CLEARC 0xFF102030UL
#define MAXV  400
#define MAXT  800
#define NCOL  7

static const unsigned char pal[NCOL][3] = {
    { 255,   0,   0 }, {   0, 255,   0 }, {   0,   0, 255 },
    { 255, 255,   0 }, { 255,   0, 255 }, {   0, 255, 255 },
    { 255, 255, 255 }
};

static double vx[MAXV], vy[MAXV];
static int tv[MAXT][3], tc[MAXT];
static int nv, nt;
static unsigned long *app;

static void softOn(void)  { glEnable(GL_SCISSOR_TEST); glScissor(0, 0, W, H); }
static void softOff(void) { glDisable(GL_SCISSOR_TEST); }

static int
readMesh(const char *path)
{
    FILE *f = fopen(path, "r");
    char line[256];
    int i;

    if (!f) return 0;
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#') continue;
        if (line[0] == 'V') {
            if (sscanf(line + 1, "%d", &nv) != 1 || nv > MAXV) { fclose(f); return 0; }
            for (i = 0; i < nv; i++)
                if (fscanf(f, "%lf %lf", &vx[i], &vy[i]) != 2) { fclose(f); return 0; }
        } else if (line[0] == 'T') {
            if (sscanf(line + 1, "%d", &nt) != 1 || nt > MAXT) { fclose(f); return 0; }
            for (i = 0; i < nt; i++)
                if (fscanf(f, "%d %d %d %d", &tv[i][0], &tv[i][1], &tv[i][2],
                           &tc[i]) != 4) { fclose(f); return 0; }
        }
    }
    fclose(f);
    return (nv > 0 && nt > 0);
}

int
main(int argc, char **argv)
{
    OSMesaContext ctx;
    const char *path = (argc > 1) ? argv[1] : "scratch-mesh/mesh.txt";
    const char *mode = (argc > 2) ? argv[2] : "hw";
    unsigned long d0, s0, x0, h0, w0;
    long x, y;
    int t, k;

    if (!readMesh(path)) { printf("cannot read %s\n", path); return 2; }

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
    glShadeModel(GL_FLAT);
    glClearColor(0x10/255.0f, 0x20/255.0f, 0x30/255.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    d0 = OSMGAMesaHookDrawn(); s0 = OSMGAMesaHookSoftware();
    x0 = OSMGAMesaHookDeclined();
    h0 = OSMGAMesaHookHardState(); w0 = OSMGAMesaHookSoftState();

    for (t = 0; t < nt; t++) {
        if (strcmp(mode, "sw") == 0) softOn();
        /* alternate the path triangle by triangle: every shared edge in the
         * mesh is then a hardware/software boundary, which is the hardest
         * arrangement this can be asked for */
        else if (strcmp(mode, "mix") == 0) { if (t & 1) softOn(); else softOff(); }
        glColor4ub(pal[tc[t] % NCOL][0], pal[tc[t] % NCOL][1],
                   pal[tc[t] % NCOL][2], 255);
        glBegin(GL_TRIANGLES);
          for (k = 0; k < 3; k++)
              glVertex2d(vx[tv[t][k]], vy[tv[t][k]]);
        glEnd();
    }
    softOff();
    glFinish();

    printf("# mesh %s mode %s tri %d\n", path, mode, nt);
    printf("# counters drawn=%lu software=%lu declined=%lu\n",
           OSMGAMesaHookDrawn() - d0, OSMGAMesaHookSoftware() - s0,
           OSMGAMesaHookDeclined() - x0);
    printf("# select hard %lu soft %lu\n",
           OSMGAMesaHookHardState() - h0, OSMGAMesaHookSoftState() - w0);
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++) {
            unsigned long c = app[y * W + x];

            if (c == CLEARC) continue;
            printf("P %ld %ld %lu %lu %lu\n", x, y,
                   (c >> 16) & 0xFFUL, (c >> 8) & 0xFFUL, c & 0xFFUL);
        }
    return 0;
}
