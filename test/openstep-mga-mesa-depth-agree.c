/*
 * openstep-mga-mesa-depth-agree.c -- how far apart are the two depths?
 *
 * The two paths share a depth buffer, which has been shown.  Whether they
 * write the SAME value at the same pixel is a different question, and one
 * that decides whether a frame may mix them: a GL_LESS falling between the
 * two answers is decided differently depending on which path drew first.
 *
 * The same triangle is drawn twice -- once through the engine, once forced
 * to software -- and the depth buffer read back each time.  What comes out
 * is a number, which is what the decision needs.
 *
 *   cc -O -Wall -o /tmp/zagree openstep-mga-mesa-depth-agree.c \
 *      -I<mesa>/include -L<built> -lGL_mga
 */

#include <stdio.h>
#include <string.h>
#include <GL/osmesa.h>

extern unsigned long OSMGAMesaHookDrawn(void);
extern unsigned long OSMGAMesaBufferDepthOrigin(void);

#define W 64
#define H 64

static unsigned short hw[W * H];
static unsigned short sw[W * H];

static void
drawIt(void)
{
    /* A gentle slope, so the two disagree by units rather than by halves. */
    glBegin(GL_TRIANGLES);
      glColor3ub(255, 255, 255);
      glVertex3f( 1.5f,  1.5f, -0.20f);
      glVertex3f(40.5f,  1.5f, -0.10f);
      glVertex3f( 1.5f, 40.5f,  0.10f);
    glEnd();
    glFinish();
}

static void
snap(unsigned short *dst, OSMesaContext ctx)
{
    GLint dw = 0, dh = 0, bpv = 0;
    void *zb = 0;

    if (!OSMesaGetDepthBuffer(ctx, &dw, &dh, &bpv, &zb) || !zb || bpv != 2) {
        printf("   no 16-bit depth buffer to read\n");
        return;
    }
    memcpy(dst, zb, (unsigned)(W * H) * sizeof(unsigned short));
}

int
main(void)
{
    OSMesaContext ctx;
    static unsigned char appbuf[W * H * 4];
    unsigned long drewA, drewB;
    long worst = 0, sum = 0, n = 0, cover = 0;
    int x, y, hist[6];

    ctx = OSMesaCreateContext(OSMESA_ARGB, NULL);
    if (!ctx || !OSMesaMakeCurrent(ctx, appbuf, GL_UNSIGNED_BYTE, W, H)) {
        printf("no context\n");
        return 1;
    }
    if (OSMGAMesaBufferDepthOrigin() == 0UL) {
        printf("no shared depth buffer -- nothing to compare\n");
        return 0;
    }
    glViewport(0, 0, W, H);
    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    glOrtho(0.0, (double)W, 0.0, (double)H, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    glEnable(GL_DEPTH_TEST);
    /*
     * GL_LESS, not GL_ALWAYS: the chooser only accepts the comparison the
     * engine performs, so asking for any other quietly puts both passes
     * through software and compares software with itself.  With the buffer
     * cleared to its default of 1.0 every fragment passes anyway, so the
     * depths still all get written.
     */
    glDepthFunc(GL_LESS);

    printf("the same triangle, drawn each way\n");

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    drewA = OSMGAMesaHookDrawn();
    drawIt();
    drewB = OSMGAMesaHookDrawn();
    printf("   first pass accelerated: %s\n",
           (drewB > drewA) ? "yes" : "NO -- the comparison is meaningless");
    snap(hw, ctx);

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_SCISSOR_TEST);       /* refused -> software */
    glScissor(0, 0, W, H);
    drewA = OSMGAMesaHookDrawn();
    drawIt();
    drewB = OSMGAMesaHookDrawn();
    glDisable(GL_SCISSOR_TEST);
    printf("   second pass accelerated: %s\n",
           (drewB > drewA) ? "YES -- the comparison is meaningless" : "no");
    snap(sw, ctx);

    for (x = 0; x < 6; x++)
        hist[x] = 0;
    for (y = 2; y < 38; y++) {
        for (x = 2; x < 38; x++) {
            long a = (long)hw[y * W + x];
            long b = (long)sw[y * W + x];
            long d;

            /*
             * Two different questions live here and the first version asked
             * them at once.  A pixel one path covered and the other did not
             * still holds the clear value on that side, and comparing 65535
             * against a real depth reports a difference of half the range --
             * which is a coverage disagreement wearing a depth error's
             * clothes.  Counted separately.
             */
            if (a == 65535L && b == 65535L)
                continue;                       /* neither covered it */
            if (a == 65535L || b == 65535L) {
                cover++;
                continue;                       /* only one did */
            }
            d = a - b;
            if (d < 0) d = -d;
            n++; sum += d;
            if (d > worst) worst = d;
            hist[(d < 5) ? (int)d : 5]++;
        }
    }
    /* Raw counts only.  Anything derived from them is worked out where it
     * can be checked, not here. */
    printf("RAW cover=%ld n=%ld sum=%ld worst=%ld h0=%d h1=%d h2=%d h3=%d h4=%d h5=%d\n",
           cover, n, sum, worst, hist[0], hist[1], hist[2], hist[3], hist[4],
           hist[5]);
    OSMesaDestroyContext(ctx);
    return 0;
}
