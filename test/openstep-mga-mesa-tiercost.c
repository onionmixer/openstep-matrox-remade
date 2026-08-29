/*
 * M15 Phase 1 -- what does a frame cost on each tier?
 *
 * The tier exists for speed.  Its picture is qualified (M12) and its three
 * differences are sized, so the question left is whether it pays.
 *
 * This measures ONE endpoint and says so: the ENQUEUE cost, with the mirror
 * suppressed.  It makes no claim about when the drawing finishes.
 *
 * Why not wall-clock minus the time inside the ioctl, which is what I first
 * planned: that residual is not the tier's userland cost, it is everything
 * outside the timed wrapper -- Mesa's dispatch, batching, replay, the timer
 * calls, pre-emption, and the MIRROR, which sits at the end of every render
 * bracket (Hook.c) and was measured elsewhere at 146 ms for 512x384.  Worse,
 * a video-memory readback can force earlier asynchronous drawing to finish,
 * so it is not an independent constant anybody may subtract.  Present mode
 * already stands the mirror down -- OSMGAMesaBufferPresentMode(1) declares
 * the caller's array stale and every path that would touch it consults that
 * and stands down -- so the endpoint is obtained by removing the mirror
 * rather than by subtracting an estimate of it.
 *
 * Nothing here is instrumented.  Two gettimeofdays a submission are about
 * nine microseconds of system call, and this tier submits per RUN where the
 * other submits per batch -- so leaving the hook's own timing on would tax
 * whichever arm submits more often, which is the very thing being compared.
 * The instrumented run is a separate, later question about MECHANISM.
 *
 * Both arms must be shown to have done the same work, or a faster arm may
 * simply have drawn less: every run prints its source triangles, the WARP
 * count, the trapezoid count, the software count and the refusals.
 *
 * Per-frame samples are kept and the MEDIAN reported, not the mean:
 * gettimeofday is a wall clock here and not monotonic, and a median
 * survives a jump that a mean does not.
 *
 *   /tmp/tiercost <mesh> <frames> [warmup]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <GL/gl.h>
#include <GL/osmesa.h>
#include "../mesa/OpenStepMGAMesaHook.h"
#include "../mesa/OpenStepMGAMesaBuffer.h"

#define W  320
#define H  240
#define MAXV  400
#define MAXT  800
#define NCOL  7
#define MAXF  200

static const unsigned char pal[NCOL][3] = {
    { 255,   0,   0 }, {   0, 255,   0 }, {   0,   0, 255 },
    { 255, 255,   0 }, { 255,   0, 255 }, {   0, 255, 255 },
    { 255, 255, 255 }
};

static double vx[MAXV], vy[MAXV];
static int tv[MAXT][3], tc[MAXT];
static int nv, nt;
static unsigned long *app;
static unsigned long us[MAXF];

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

static void
frame(void)
{
    int t, k;

    glClear(GL_COLOR_BUFFER_BIT);
    for (t = 0; t < nt; t++) {
        glColor4ub(pal[tc[t] % NCOL][0], pal[tc[t] % NCOL][1],
                   pal[tc[t] % NCOL][2], 255);
        glBegin(GL_TRIANGLES);
          for (k = 0; k < 3; k++)
              glVertex2d(vx[tv[t][k]], vy[tv[t][k]]);
        glEnd();
    }
    glFinish();
}

static int
cmpul(const void *a, const void *b)
{
    unsigned long x = *(const unsigned long *)a;
    unsigned long y = *(const unsigned long *)b;

    return (x < y) ? -1 : ((x > y) ? 1 : 0);
}

int
main(int argc, char **argv)
{
    OSMesaContext ctx;
    const char *path  = (argc > 1) ? argv[1] : "scratch-mesh/mesh16.txt";
    int         want  = (argc > 2) ? atoi(argv[2]) : 20;
    int         warm  = (argc > 3) ? atoi(argv[3]) : 3;
    unsigned long d0, w0, s0, x0, t0c, st0[6], st1[6];
    struct timeval a, b;
    int i;

    if (want > MAXF) want = MAXF;
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

    /* The mirror, stood down.  This is what makes the number an enqueue
     * cost and not a delivery cost, and the array is stale from here. */
    OSMGAMesaBufferPresentMode(1);

    /* Warm-up, discarded: the first submission of a run places the WARP
     * microcode, and a one-off has no business in a per-frame number.
     * Cold and warm are both real answers -- this file reports the warm
     * one and says so. */
    for (i = 0; i < warm; i++)
        frame();

    d0 = OSMGAMesaHookDrawn();   w0 = OSMGAMesaHookWarp();
    s0 = OSMGAMesaHookSoftware(); x0 = OSMGAMesaHookDeclined();
    t0c = OSMGAMesaHookTraps();
    OSMGAMesaHookSubmitStats(st0);

    for (i = 0; i < want; i++) {
        gettimeofday(&a, (struct timezone *)0);
        frame();
        gettimeofday(&b, (struct timezone *)0);
        us[i] = (unsigned long)((b.tv_sec - a.tv_sec) * 1000000L +
                                (b.tv_usec - a.tv_usec));
    }
    OSMGAMesaHookSubmitStats(st1);

    qsort(us, (size_t)want, sizeof us[0], cmpul);

    printf("# tiercost mesh %s  triangles %d  frames %d  warmup %d\n",
           path, nt, want, warm);
    printf("# work  source %d/frame  drawn %lu  warp %lu  traps %lu"
           "  software %lu  declined %lu\n",
           nt, OSMGAMesaHookDrawn() - d0, OSMGAMesaHookWarp() - w0,
           OSMGAMesaHookTraps() - t0c, OSMGAMesaHookSoftware() - s0,
           OSMGAMesaHookDeclined() - x0);
    printf("# submits %lu  dwords %lu  per frame %lu.%02lu submits"
           "  %lu dwords\n",
           st1[0] - st0[0], st1[2] - st0[2],
           (st1[0] - st0[0]) / (unsigned long)want,
           (((st1[0] - st0[0]) % (unsigned long)want) * 100UL)
               / (unsigned long)want,
           (st1[2] - st0[2]) / (unsigned long)want);
    printf("# frame us  min %lu  p25 %lu  median %lu  p75 %lu  max %lu\n",
           us[0], us[want / 4], us[want / 2], us[(3 * want) / 4],
           us[want - 1]);
    for (i = 0; i < want; i++)
        printf("F %d %lu\n", i, us[i]);
    return 0;
}
