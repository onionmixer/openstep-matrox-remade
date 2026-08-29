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

/*
 * The header rather than a handful of externs written out here.  The forcing
 * function was being called with NO declaration in scope -- C89 makes that an
 * implicit int, which happens to work for one int argument and would stop
 * working silently the day the signature moved.
 */
#include "../mesa/OpenStepMGAMesaHook.h" 

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

    /*
     * "sw" forces once, for the whole loop, and the flush at the end is
     * therefore inside the forced region already.
     */
    if (strcmp(mode, "sw") == 0) softOn();

    for (t = 0; t < nt; t++) {
        /* alternate the path triangle by triangle: every shared edge in the
         * mesh is then a hardware/software boundary, which is the hardest
         * arrangement this can be asked for */
        if (strcmp(mode, "mix") == 0) { if (t & 1) softOn(); else softOff(); }
        glColor4ub(pal[tc[t] % NCOL][0], pal[tc[t] % NCOL][1],
                   pal[tc[t] % NCOL][2], 255);
        glBegin(GL_TRIANGLES);
          for (k = 0; k < 3; k++)
              glVertex2d(vx[tv[t][k]], vy[tv[t][k]]);
        glEnd();
        /*
         * And rasterise it NOW, while this triangle's choice is still the one
         * in force.
         *
         * glEnd does not draw anything.  Mesa marks the primitive in an
         * immediate buffer (vbfill.c, gl_End) and rasterises when that buffer
         * nearly fills (gl_Begin: "IM->Count > VB_MAX-4") or when something
         * flushes it -- so the path that draws a triangle is the one selected
         * at FLUSH time, not here.
         *
         * Without this the loop toggled a flag that decided nothing.  It
         * showed, and the numbers were the proof: "sw" came out drawn=57
         * software=71 on a 128-triangle mesh rather than 0 and 128, and "mix"
         * produced the IDENTICAL counters -- the same tail flush deciding
         * both, after softOff().  The mixed frame this mode is for had never
         * been built.
         *
         * glFlush rather than glFinish because a flush is the ordering
         * boundary this needs; on this back end they cost the same, since
         * both are hooked to the mirror -- and so is RenderFinish, so a batch
         * boundary was always going to mirror.  That is what makes this
         * expensive and why the mesh runs are their own script rather than
         * part of the quick regression.
         */
        if (strcmp(mode, "mix") == 0)
            glFlush();
    }
    glFinish();
    softOff();

    printf("# mesh %s mode %s tri %d\n", path, mode, nt);
    printf("# counters drawn=%lu software=%lu declined=%lu\n",
           OSMGAMesaHookDrawn() - d0, OSMGAMesaHookSoftware() - s0,
           OSMGAMesaHookDeclined() - x0);
    printf("# select hard %lu soft %lu\n",
           OSMGAMesaHookHardState() - h0, OSMGAMesaHookSoftState() - w0);
    /*
     * What the submissions cost the kernel to encode.
     *
     * The list size is the whole subject of the state tracking, and this
     * is the biggest repeatable load in the tree -- 512 triangles, always
     * the same ones.  submitCount and submitDwords are outside the
     * instrumentation switch, so this costs nothing to read.
     */
    {
        unsigned long st[6];

        OSMGAMesaHookSubmitStats(st);
        printf("# submits %lu dwords %lu  dwords/submit %lu.%02lu\n",
               st[0], st[2],
               st[0] ? st[2] / st[0] : 0UL,
               st[0] ? ((st[2] % st[0]) * 100UL) / st[0] : 0UL);
    }
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++) {
            unsigned long c = app[y * W + x];

            if (c == CLEARC) continue;
            printf("P %ld %ld %lu %lu %lu\n", x, y,
                   (c >> 16) & 0xFFUL, (c >> 8) & 0xFFUL, c & 0xFFUL);
        }
    return 0;
}
