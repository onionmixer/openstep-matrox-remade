/*
 * M17 -- where do the two paths start to disagree?
 *
 * Draws every triangle of test/sliver-gen.py's stratified family, one at a
 * time into a cleared surface, and dumps what each covered.  It judges
 * nothing: python compares two runs of this, one per arm, and maps the
 * disagreement against the candidate quantities.
 *
 * This is CHARACTERISATION.  The review of M17 was clear that near a raster
 * boundary the outcome is discrete and phase-sensitive, so the set of
 * shapes one path gets wrong may be disconnected islands rather than "some
 * quantity below a threshold" -- and a sweep built to find a threshold
 * would find one whether or not it generalises.  So the family is
 * stratified over altitude, length, angle and sub-pixel phase, and the
 * analysis is allowed to conclude that nothing simple separates them.
 *
 * The window coordinates the hook was HANDED are recorded for every
 * triangle, because Mesa's transform can move an intended coordinate and an
 * oracle fed the requested vertices would be scoring a different triangle
 * -- which is why openstep-mga-mesa-coord-probe.c exists.
 *
 * Per-triangle counters too: which tier took it, and whether it fell back.
 * A triangle the WARP arm declined is not evidence about WARP.
 *
 *   /tmp/wfam <family file>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <GL/gl.h>
#include <GL/osmesa.h>
#include "../mesa/OpenStepMGAMesaHook.h"

extern double OSMGAMesaHookLastWin(unsigned long v, unsigned long c);

#define W  320
#define H  240
#define MAXF 1024
#define CLEARC 0xFF1A1A1AUL

static unsigned long *app;
static double fx[MAXF][3], fy[MAXF][3];
static double falt[MAXF], flen[MAXF], fang[MAXF], fph[MAXF];
static int nf;

static int
readFamily(const char *path)
{
    FILE *f = fopen(path, "r");
    char line[512];
    int i, idx;

    if (!f) return 0;
    while (fgets(line, sizeof line, f)) {
        if (line[0] == 'N') { (void)sscanf(line + 1, "%d", &nf); continue; }
        if (line[0] != 'F') continue;
        i = 0;
        if (sscanf(line + 1, "%d %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf",
                   &idx, &falt[0], &flen[0], &fang[0], &fph[0],
                   &fx[0][0], &fy[0][0], &fx[0][1], &fy[0][1],
                   &fx[0][2], &fy[0][2]) != 11) { fclose(f); return 0; }
        if (idx < 0 || idx >= MAXF) { fclose(f); return 0; }
        /* re-read into the right slot; the first parse only proved shape */
        (void)sscanf(line + 1, "%d %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf",
                     &idx, &falt[idx], &flen[idx], &fang[idx], &fph[idx],
                     &fx[idx][0], &fy[idx][0], &fx[idx][1], &fy[idx][1],
                     &fx[idx][2], &fy[idx][2]);
        (void)i;
    }
    fclose(f);
    return (nf > 0 && nf <= MAXF);
}

int
main(int argc, char **argv)
{
    OSMesaContext ctx;
    const char *path = (argc > 1) ? argv[1] : "scratch-sliver/family.txt";
    int t, k;
    long x, y;

    if (!readFamily(path)) { printf("cannot read %s\n", path); return 2; }
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
    glClearColor(0x1A/255.0f, 0x1A/255.0f, 0x1A/255.0f, 1.0f);

    printf("# family %s  %d triangles\n", path, nf);
    for (t = 0; t < nf; t++) {
        unsigned long d0 = OSMGAMesaHookDrawn();
        unsigned long w0 = OSMGAMesaHookWarp();
        unsigned long p0 = OSMGAMesaHookTraps();
        unsigned long s0 = OSMGAMesaHookSoftware();

        glClear(GL_COLOR_BUFFER_BIT);
        glColor4ub(0xE0, 0x40, 0x20, 255);
        glBegin(GL_TRIANGLES);
          for (k = 0; k < 3; k++)
              glVertex2d(fx[t][k], fy[t][k]);
        glEnd();
        glFinish();

        printf("T %d drawn %lu warp %lu traps %lu soft %lu\n", t,
               OSMGAMesaHookDrawn() - d0, OSMGAMesaHookWarp() - w0,
               OSMGAMesaHookTraps() - p0, OSMGAMesaHookSoftware() - s0);
        for (k = 0; k < 3; k++)
            printf("V %d %d %.17g %.17g\n", t, k,
                   OSMGAMesaHookLastWin((unsigned long)k, 0),
                   OSMGAMesaHookLastWin((unsigned long)k, 1));
        for (y = 0; y < H; y++)
            for (x = 0; x < W; x++)
                if (app[y * W + x] != CLEARC)
                    printf("P %d %ld %ld\n", t, x, y);
    }
    OSMesaDestroyContext(ctx);
    free(app);
    return 0;
}
