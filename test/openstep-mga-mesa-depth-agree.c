/*
 * openstep-mga-mesa-depth-agree.c -- how far apart are the two depths, and
 * does it matter which one drew first?
 *
 * The two paths share a depth buffer, which has been shown.  Whether they
 * write the SAME value at the same pixel is a different question, and one
 * that decides whether a frame may mix them: a GL_LESS falling between the
 * two answers is decided differently depending on which path drew first.
 *
 * Four passes, each into a freshly cleared buffer:
 *
 *   1  accelerated alone
 *   2  software alone
 *   3  software, then accelerated over it
 *   4  accelerated, then software over it
 *
 * The first two say how far apart the answers are.  The last two say what
 * happens when they meet, and the claim they test is that both should come
 * out as the per-pixel MINIMUM of the first two -- which is what GL_LESS
 * means and is not something either order should be able to change.
 *
 * The size is an argument.  It used to be 64x64, which is a size at which
 * the depth buffer's offset happens to land on a page boundary, so every
 * measurement ever taken here was of the one shape that could not fail.
 *
 * The triangle is drawn twice, once at the origin and once near the far
 * corner, and both are compared.  The near copy alone was 1296 words of a
 * 480000-word buffer at 800x600 -- a quarter of one per cent, all of it in
 * one corner -- while the thing under test is an allocation whose origin and
 * size both changed.  A wrong origin or a short mapping shows in the last
 * rows, and nothing was reading them.
 *
 * Raw counts only.  Anything derived from them is worked out where it can be
 * checked, not here.
 *
 *   cc -O -Wall -o /tmp/zagree openstep-mga-mesa-depth-agree.c \
 *      -I<mesa>/include -L<built> -lGL_mga
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <GL/osmesa.h>

extern unsigned long OSMGAMesaHookDrawn(void);
extern unsigned long OSMGAMesaBufferDepthOrigin(void);

/* The triangle occupies window 1.5 .. 40.5 on both axes; the comparison
 * looks at 2 .. 37, inside it on every side.  The far copy is inset by 42
 * from each edge, which needs a buffer of at least 42 + 37 + 1. */
#define SCAN_LO     2
#define SCAN_HI     37
#define SHIFT       42
#define NEED_FAR    (SHIFT + SCAN_HI + 1)

#define CLEARZ      65535

static int W, H;
static unsigned short *snapA, *snapB, *mix1, *mix2;

static void
drawOne(double dx, double dy)
{
    /* A gentle slope, so the two disagree by units rather than by halves. */
    glBegin(GL_TRIANGLES);
      glColor3ub(255, 255, 255);
      glVertex3f((float)(dx +  1.5), (float)(dy +  1.5), -0.20f);
      glVertex3f((float)(dx + 40.5), (float)(dy +  1.5), -0.10f);
      glVertex3f((float)(dx +  1.5), (float)(dy + 40.5),  0.10f);
    glEnd();
}

static int
haveFar(void)
{
    return W >= NEED_FAR && H >= NEED_FAR;
}

static void
drawIt(void)
{
    drawOne(0.0, 0.0);
    if (haveFar())
        drawOne((double)(W - SHIFT), (double)(H - SHIFT));
    glFinish();
}

/* Software is forced by turning on a scissor that covers the whole surface:
 * the chooser refuses any raster bit it has not been shown to handle, and
 * SCISSOR_BIT is not among the three it allows, while a box the size of the
 * surface clips nothing.  So the path changes and the picture does not. */
static void
softOn(void)  { glEnable(GL_SCISSOR_TEST); glScissor(0, 0, W, H); }
static void
softOff(void) { glDisable(GL_SCISSOR_TEST); }

static int
snap(unsigned short *dst, OSMesaContext ctx)
{
    GLint dw = 0, dh = 0, bpv = 0;
    void *zb = 0;

    if (!OSMesaGetDepthBuffer(ctx, &dw, &dh, &bpv, &zb) || !zb || bpv != 2) {
        printf("   no 16-bit depth buffer to read\n");
        return 0;
    }
    memcpy(dst, zb, (unsigned)(W * H) * sizeof(unsigned short));
    return 1;
}

/* How many words hold the clear value, so the baseline is counted rather
 * than assumed. */
static long
clearWords(OSMesaContext ctx)
{
    GLint dw = 0, dh = 0, bpv = 0;
    void *zb = 0;
    long n = 0, i;
    const unsigned short *z;

    if (!OSMesaGetDepthBuffer(ctx, &dw, &dh, &bpv, &zb) || !zb || bpv != 2)
        return -1;
    z = (const unsigned short *)zb;
    for (i = 0; i < (long)W * H; i++)
        if (z[i] == CLEARZ) n++;
    return n;
}

/* Was that pass drawn by the engine?  Asked of every pass: a first pass that
 * was accelerated proves nothing about whether a later state change got the
 * hardware back. */
static unsigned long hookMark;
static void
markBegin(void) { hookMark = OSMGAMesaHookDrawn(); }
static int
markAccel(void) { return OSMGAMesaHookDrawn() > hookMark; }

int
main(int argc, char **argv)
{
    OSMesaContext ctx;
    void *appbuf;
    long region, x, y, x0, y0;
    long hwOnly = 0, swOnly = 0, lt = 0, eq = 0, gt = 0;
    long mix1Changed = 0, mix2Changed = 0, mix1NotMin = 0, mix2NotMin = 0;
    long worst = 0, sum = 0, n = 0;
    int a1, a2, a3s, a3h, a4h, a4s;

    W = (argc > 1) ? atoi(argv[1]) : 64;
    H = (argc > 2) ? atoi(argv[2]) : 64;
    if (W < 42 || H < 42) { printf("the triangle needs at least 42x42\n"); return 2; }

    appbuf = malloc((unsigned)(W * H * 4));
    snapA  = (unsigned short *)malloc((unsigned)(W * H) * sizeof(unsigned short));
    snapB  = (unsigned short *)malloc((unsigned)(W * H) * sizeof(unsigned short));
    mix1   = (unsigned short *)malloc((unsigned)(W * H) * sizeof(unsigned short));
    mix2   = (unsigned short *)malloc((unsigned)(W * H) * sizeof(unsigned short));
    if (!appbuf || !snapA || !snapB || !mix1 || !mix2) {
        printf("no room for %dx%d\n", W, H); return 2;
    }
    memset(appbuf, 0, (unsigned)(W * H * 4));

    ctx = OSMesaCreateContext(OSMESA_ARGB, NULL);
    if (!ctx || !OSMesaMakeCurrent(ctx, appbuf, GL_UNSIGNED_BYTE, W, H)) {
        printf("no context at %dx%d\n", W, H); return 2;
    }
    if (OSMGAMesaBufferDepthOrigin() == 0UL) {
        printf("%dx%d: no shared depth buffer -- nothing to compare\n", W, H);
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
     * through software and compares software with itself.  Both of the
     * others are the default, and are set anyway so that a later default
     * cannot move underneath this.
     */
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glClearDepth(1.0);

    printf("%dx%d  depth origin %lu  far copy %s\n", W, H,
           OSMGAMesaBufferDepthOrigin(), haveFar() ? "yes" : "no (too small)");

    /* 1: accelerated alone */
    softOff();
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    printf("   words holding the clear value after a clear: %ld of %ld\n",
           clearWords(ctx), (long)W * H);
    markBegin(); drawIt(); a1 = markAccel();
    if (!snap(snapA, ctx)) return 2;

    /* 2: software alone */
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    softOn();
    markBegin(); drawIt(); a2 = markAccel();
    softOff();
    if (!snap(snapB, ctx)) return 2;

    /* 3: software first, then accelerated over it */
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    softOn();
    markBegin(); drawIt(); a3s = markAccel();
    softOff();
    markBegin(); drawIt(); a3h = markAccel();
    if (!snap(mix1, ctx)) return 2;

    /* 4: the other order */
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    markBegin(); drawIt(); a4h = markAccel();
    softOn();
    markBegin(); drawIt(); a4s = markAccel();
    softOff();
    if (!snap(mix2, ctx)) return 2;

    printf("   accelerated?  pass1=%d pass2=%d  pass3 sw=%d hw=%d  "
           "pass4 hw=%d sw=%d\n", a1, a2, a3s, a3h, a4h, a4s);
    printf("   (pass1, pass3 hw and pass4 hw want 1; the rest want 0)\n");

    for (region = 0; region < (haveFar() ? 2 : 1); region++) {
        x0 = region ? (long)(W - SHIFT) : 0L;
        y0 = region ? (long)(H - SHIFT) : 0L;

        for (y = y0 + SCAN_LO; y <= y0 + SCAN_HI; y++) {
            for (x = x0 + SCAN_LO; x <= x0 + SCAN_HI; x++) {
                long i = y * (long)W + x;
                long a = (long)snapA[i];      /* accelerated alone */
                long b = (long)snapB[i];      /* software alone */
                long m1 = (long)mix1[i], m2 = (long)mix2[i];
                long lo = (a < b) ? a : b;
                long d;

                /*
                 * Coverage and value are two questions and the first version
                 * of this asked them at once.  A pixel one path covered and
                 * the other did not still holds the clear value on that
                 * side, and comparing it against a real depth reports a
                 * difference of half the range -- a coverage disagreement
                 * wearing a depth error's clothes.  Counted apart.
                 */
                if (a == CLEARZ && b == CLEARZ) {
                    /* neither covered it; the mixes should not have either */
                    if (m1 != CLEARZ) mix1Changed++;
                    if (m2 != CLEARZ) mix2Changed++;
                    continue;
                }
                if (b == CLEARZ) hwOnly++;
                else if (a == CLEARZ) swOnly++;
                else {
                    if (a < b) lt++; else if (a == b) eq++; else gt++;
                    d = a - b; if (d < 0) d = -d;
                    n++; sum += d; if (d > worst) worst = d;
                }
                /*
                 * What the mixes must come to.  Whichever drew first, a
                 * GL_LESS frame ends at the smaller of the two answers, and
                 * at the clear value only where neither covered.  Counted
                 * rather than asserted so that a disagreement is a number.
                 */
                if (m1 != lo) mix1NotMin++;
                if (m2 != lo) mix2NotMin++;
                if (m1 != b)  mix1Changed++;   /* pass 3 changed it after sw */
                if (m2 != a)  mix2Changed++;   /* pass 4 changed it after hw */
            }
        }
    }

    printf("RAW %dx%d regions=%d hwOnly=%ld swOnly=%ld lt=%ld eq=%ld gt=%ld "
           "n=%ld sum=%ld worst=%ld mix1Changed=%ld mix2Changed=%ld "
           "mix1NotMin=%ld mix2NotMin=%ld\n",
           W, H, haveFar() ? 2 : 1, hwOnly, swOnly, lt, eq, gt,
           n, sum, worst, mix1Changed, mix2Changed, mix1NotMin, mix2NotMin);

    OSMesaDestroyContext(ctx);
    return 0;
}
