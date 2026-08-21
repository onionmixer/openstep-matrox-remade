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

int
main(void)
{
    OSMesaContext ctx;
    unsigned long *app;
    long i, lowX = 0, lowY = 0, hiX = 0, hiY = 0;

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
    glDisable(GL_CULL_FACE); glShadeModel(GL_FLAT);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

    printf("# surface %dx%d  origin %lu\n", W, H, OSMGAMesaBufferOrigin());
    printf("# asked  -> window coordinate the hook received -> its (long)\n");

    for (i = 0; i < (H > W ? H : W); i++) {
        double wx, wy;
        long cx, cy;

        glClear(GL_COLOR_BUFFER_BIT);
        glBegin(GL_TRIANGLES);
          glColor4ub(0, 255, 0, 255);
          /* one vertex at the position under test, the other two well away
             and at positions that are not themselves in question */
          glVertex3f((float)(i < W ? i : 0), (float)(i < H ? i : 0), 0.0f);
          glVertex3f(64.0f, 128.0f, 0.0f);
          glVertex3f(192.0f, 200.0f, 0.0f);
        glEnd();
        glFinish();
        if (OSMGAMesaHookDrawn() == 0UL) { printf("# nothing accelerated\n"); break; }
        wx = OSMGAMesaHookLastWin(0, 0);
        wy = OSMGAMesaHookLastWin(0, 1);
        cx = (long)wx; cy = (long)wy;
        if (i < W && cx != i) { lowX++; if (lowX < 6) printf("X %ld %.9f %ld\n", i, wx, cx); }
        if (i < H && cy != i) { lowY++; if (lowY < 6) printf("Y %ld %.9f %ld\n", i, wy, cy); }
        if (i < W) hiX++;
        if (i < H) hiY++;
        printf("C %ld %.9f %.9f\n", i, wx, wy);
    }
    printf("# x: %ld of %ld positions lost by the cast\n", lowX, hiX);
    printf("# y: %ld of %ld positions lost by the cast\n", lowY, hiY);
    OSMesaDestroyContext(ctx);
    return 0;
}
