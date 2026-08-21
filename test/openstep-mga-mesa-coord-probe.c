/*
 * openstep-mga-mesa-coord-probe.c -- what does the hook actually receive?
 *
 * A triangle asked for at integer coordinates came out 83 pixels too large
 * because one vertex arrived at y = 9 when 10 was asked for.  Reasoning about
 * that failed: recomputing Mesa's viewport transform is not Mesa's arithmetic.
 * So the hook now records the window coordinates it is handed, as floats and
 * before its cast, and this walks every integer position and reads them back.
 *
 *   cc -O -Wall -o /tmp/cp openstep-mga-mesa-coord-probe.c \
 *      -I<mesa>/include -L<built> -lGL_mga
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <GL/osmesa.h>

extern double OSMGAMesaHookLastWin(unsigned long v, unsigned long c);
extern unsigned long OSMGAMesaHookDrawn(void);
extern unsigned long OSMGAMesaBufferOrigin(void);

#define W  320
#define H  240

/*
 * Several viewport shapes, not one.  A single 320x240 sweep is thin evidence:
 * the arithmetic that produces the noise is a matrix multiply whose scale and
 * bias come from the viewport, so odd sizes and a non-zero origin are
 * different arithmetic, not the same arithmetic again.
 */
static const int vpx[4] = {   0,   0,  17,   0 };
static const int vpy[4] = {   0,   0,  11,   0 };
static const int vpw[4] = { 320, 319, 200, 640 };
static const int vph[4] = { 240, 239, 150, 480 };

int
main(void)
{
    OSMesaContext ctx;
    unsigned long *app;
    long i, lowX = 0, lowY = 0, hiX = 0, hiY = 0;
    int cfg;

    app = (unsigned long *)malloc((unsigned)(W * H) * sizeof(unsigned long));
    if (!app) { printf("no room\n"); return 2; }
    ctx = OSMesaCreateContext(OSMESA_ARGB, NULL);
    if (!ctx || !OSMesaMakeCurrent(ctx, app, GL_UNSIGNED_BYTE, W, H)) {
        printf("no context\n"); return 2;
    }
    glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND); glDisable(GL_DITHER);
    glDisable(GL_CULL_FACE); glShadeModel(GL_FLAT);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

    printf("# surface %dx%d  origin %lu\n", W, H, OSMGAMesaBufferOrigin());

    /* ---- depth first: what noise does the window z carry? ---------------
     * The calibration says code = round(32767.5 * (1 - objz)), so an object
     * depth of 1 - k/32767.5 should arrive as exactly k.  Ask for many k and
     * see what turns up, since the depth coordinate goes through the same
     * cast on the same line as x and y.
     */
    glViewport(0, 0, W, H);
    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    glOrtho(0.0, (double)W, 0.0, (double)H, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    for (i = 0; i <= 65535L; i += 337L) {
        double wz;

        glClear(GL_COLOR_BUFFER_BIT);
        glBegin(GL_TRIANGLES);
          glColor4ub(0, 255, 0, 255);
          glVertex3d(64.0, 64.0, 1.0 - (double)i / 32767.5);
          glVertex3d(200.0, 64.0, 1.0 - (double)i / 32767.5);
          glVertex3d(128.0, 200.0, 1.0 - (double)i / 32767.5);
        glEnd();
        glFinish();
        if (OSMGAMesaHookDrawn() == 0UL) break;
        wz = OSMGAMesaHookLastWin(0, 2);
        printf("Z %ld %.6f\n", i, wz);
    }

    /* ---- then x and y, in several viewports ---------------------------- */
    for (cfg = 0; cfg < 4; cfg++) {
      printf("# viewport %d %d %d %d\n", vpx[cfg], vpy[cfg], vpw[cfg], vph[cfg]);
      glViewport(vpx[cfg], vpy[cfg], vpw[cfg], vph[cfg]);
      glMatrixMode(GL_PROJECTION); glLoadIdentity();
      glOrtho(0.0, (double)vpw[cfg], 0.0, (double)vph[cfg], -1.0, 1.0);
      glMatrixMode(GL_MODELVIEW); glLoadIdentity();
      for (i = 0; i < (vph[cfg] > vpw[cfg] ? vph[cfg] : vpw[cfg]); i++) {
        double wx, wy;
        long cx, cy;

        glClear(GL_COLOR_BUFFER_BIT);
        glBegin(GL_TRIANGLES);
          glColor4ub(0, 255, 0, 255);
          glVertex3f((float)(i < vpw[cfg] ? i : 0),
                     (float)(i < vph[cfg] ? i : 0), 0.0f);
          glVertex3f(32.0f, 64.0f, 0.0f);
          glVertex3f(96.0f, 100.0f, 0.0f);
        glEnd();
        glFinish();
        if (OSMGAMesaHookDrawn() == 0UL) continue;
        wx = OSMGAMesaHookLastWin(0, 0);
        wy = OSMGAMesaHookLastWin(0, 1);
        cx = (long)wx; cy = (long)wy;
        /* the vertex lands at viewport origin + i, so that is what is asked */
        if (i < vpw[cfg]) { hiX++; if (cx != vpx[cfg] + i) lowX++; }
        if (i < vph[cfg]) { hiY++; if (cy != vpy[cfg] + i) lowY++; }
        printf("C %d %ld %.9f %.9f\n", cfg, i, wx, wy);
      }
    }
    printf("# x: %ld of %ld positions lost by the cast\n", lowX, hiX);
    printf("# y: %ld of %ld positions lost by the cast\n", lowY, hiY);
    OSMesaDestroyContext(ctx);
    return 0;
}
