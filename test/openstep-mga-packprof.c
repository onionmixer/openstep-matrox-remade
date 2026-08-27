/*
 * W2-19.11 -- what does packing WARP vertices cost?
 *
 * "WARP gives 1.43x" has always been an upper bound resting on the packing
 * being free, and section 22 said so.  This measures it.  Nothing here opens
 * a device, maps anything, or submits anything: packing is host code, so it
 * is measured on the host, exactly as the trapezoid build was in buildprof.
 * No scissor, and the clock is read twice per run.
 *
 * WHAT IS BEING PACKED, AND WHY IT IS THE REFERENCE'S VERSION AND NOT MINE.
 *
 * scratch/mga-dri-xf410/mgavb.h:55 gives the record and mgavb.c:57-92 gives
 * the code that fills it.  Untextured, no specular, no fog, that is:
 *
 *     v->v.rhw = win[3];
 *     v->v.z   = depth_scale * win[2];
 *     v->v.x   = win[0] + xoffset;
 *     v->v.y   = - win[1] + yoffset;
 *     v->v.color.blue  = col[2];
 *     v->v.color.green = col[1];
 *     v->v.color.red   = col[0];
 *     v->v.color.alpha = col[3];
 *
 * One multiply, two adds, one negate and four byte moves.  Inventing a packer
 * and timing it would measure the invention; this transcribes the one the
 * G400 DRI driver actually shipped.
 *
 * THERE IS NO SEPARATE COPY.  mga_vertex_buffer_t holds the array the ioctl
 * hands to the card, so the packed array IS the DMA source.  Packing is the
 * whole host-side cost, and this programme does not add a memcpy the
 * reference does not do.
 *
 * WHAT IT IS COMPARED AGAINST.  A WARP path would replace the CPU trapezoid
 * build (4.38 ms) and the kernel's encode (1.98 ms) -- 6.36 ms a frame.  If
 * packing costs more than that, WARP is a loss.  That is the question.
 *
 * HOW MANY VERTICES.  Our hook sees triangles one at a time through Mesa's
 * TriangleFunc, so it would pack 3 per triangle: 2937 a frame at grid 4.  The
 * DRI driver instead packs a vertex BUFFER once per range, so a shared mesh
 * vertex is packed once.  Both are reported, because which one applies
 * depends on where a WARP path hooks into Mesa, and that is a design choice
 * this measurement should not prejudge.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

typedef unsigned char  u8;
typedef unsigned long  u32;

/* mgavb.h:38 -- the colour, in the card's byte order. */
typedef struct {
    u8 blue, green, red, alpha;
} warpColor;

/*
 * mgavb.h:55.  Sixteen floats in the union because the fastpath expects that
 * stride; eight dwords of it are what the card transfers (WVRTXSZ 0x1807).
 * Both are represented, because the stride is what the packer walks and the
 * eight dwords are what has to be written.
 */
typedef struct {
    float     x, y, z;
    float     rhw;
    warpColor color;
    warpColor specular;
    float     tu0, tv0;
    float     tu1, tv1;
} warpVertex;

typedef union {
    warpVertex v;
    float      f[16];
    u32        ui[16];
} mgaVertex;

/* What Mesa hands over: window coordinates and a colour, which is what the
 * reference reads (VB->Win.data[i] and VB->Color[0]->data[i]). */
typedef struct { float win[4]; u8 col[4]; } mesaVert;

static double
nowMs(void)
{
    struct timeval tv;

    gettimeofday(&tv, (struct timezone *)0);
    return (double)tv.tv_sec * 1000.0 + (double)tv.tv_usec / 1000.0;
}

static unsigned long lcgState;

static unsigned long
lcg(void)
{
    lcgState = lcgState * 1664525UL + 1013904223UL;
    return lcgState;
}

/*
 * mgavb.c:57-92, untextured, no specular, no fog.  Transcribed, not invented.
 */
static void
packPlain(mgaVertex *v, const mesaVert *m, unsigned long n,
          float depthScale, float xoffset, float yoffset)
{
    unsigned long i;

    for (i = 0UL; i < n; i++, v++) {
        const float *win = m[i].win;
        const u8 *col = m[i].col;

        v->v.rhw = win[3];
        v->v.z   = depthScale * win[2];
        v->v.x   = win[0] + xoffset;
        v->v.y   = -win[1] + yoffset;
        v->v.color.blue  = col[2];
        v->v.color.green = col[1];
        v->v.color.red   = col[0];
        v->v.color.alpha = col[3];
    }
}

/* The same with texture coordinates, which mgavb.c adds as TEX0. */
static void
packTex(mgaVertex *v, const mesaVert *m, unsigned long n,
        float depthScale, float xoffset, float yoffset,
        const float (*tc)[2])
{
    unsigned long i;

    for (i = 0UL; i < n; i++, v++) {
        const float *win = m[i].win;
        const u8 *col = m[i].col;

        v->v.rhw = win[3];
        v->v.z   = depthScale * win[2];
        v->v.x   = win[0] + xoffset;
        v->v.y   = -win[1] + yoffset;
        v->v.color.blue  = col[2];
        v->v.color.green = col[1];
        v->v.color.red   = col[0];
        v->v.color.alpha = col[3];
        v->v.tu0 = tc[i][0];
        v->v.tv0 = tc[i][1];
    }
}

static void
run(const char *label, unsigned long n, unsigned long reps, int textured)
{
    mesaVert *src;
    mgaVertex *dst;
    float (*tc)[2];
    unsigned long i, r;
    double t0, t1, ms;

    lcgState = 12345UL;                 /* reseeded per run, as buildprof does */
    src = (mesaVert *)malloc(sizeof(mesaVert) * n);
    dst = (mgaVertex *)malloc(sizeof(mgaVertex) * n);
    tc  = (float (*)[2])malloc(sizeof(float) * 2UL * n);
    if (src == 0 || dst == 0 || tc == 0) {
        printf("packprof: out of memory\n");
        exit(1);
    }
    for (i = 0UL; i < n; i++) {
        src[i].win[0] = (float)(lcg() % 800UL);
        src[i].win[1] = (float)(lcg() % 600UL);
        src[i].win[2] = (float)(lcg() % 65536UL) / 65536.0f;
        src[i].win[3] = 1.0f;
        src[i].col[0] = (u8)(lcg() & 0xFF);
        src[i].col[1] = (u8)(lcg() & 0xFF);
        src[i].col[2] = (u8)(lcg() & 0xFF);
        src[i].col[3] = 255;
        tc[i][0] = (float)(lcg() % 1024UL) / 1024.0f;
        tc[i][1] = (float)(lcg() % 1024UL) / 1024.0f;
    }

    t0 = nowMs();
    for (r = 0UL; r < reps; r++) {
        if (textured)
            packTex(dst, src, n, 1.0f / 65536.0f, 0.5f, 600.5f, tc);
        else
            packPlain(dst, src, n, 1.0f / 65536.0f, 0.5f, 600.5f);
    }
    t1 = nowMs();

    ms = t1 - t0;
    printf("  %-22s %8lu vertices x %4lu reps  %9.3f ms  %8.4f us/vertex\n",
           label, n, reps, ms, ms * 1000.0 / ((double)n * (double)reps));

    free(src);
    free(dst);
    free(tc);
}

int
main(int argc, char **argv)
{
    unsigned long n = 3000UL, reps = 2000UL;

    if (argc > 1) reps = (unsigned long)atol(argv[1]);

    printf("packprof: WARP vertex packing, transcribed from "
           "mga-dri-xf410/mgavb.c\n");
    printf("packprof: no device opened, no submission made, "
           "clock read twice per run\n");
    printf("packprof: record %lu bytes, union stride %lu bytes\n\n",
           (unsigned long)sizeof(warpVertex), (unsigned long)sizeof(mgaVertex));

    printf("A. the cost, and the measurement floor beside it\n");
    run("plain",     n, reps, 0);
    run("plain",     n, reps, 0);
    run("plain",     n, reps, 0);
    run("plain",     n, reps, 0);
    run("textured",  n, reps, 1);

    printf("\nB. does it scale with the count, or is there a fixed cost?\n");
    run("300",       300UL,   reps * 10UL, 0);
    run("3000",      3000UL,  reps,        0);
    run("30000",     30000UL, reps / 10UL, 0);

    printf("\npackprof: done\n");
    return 0;
}
