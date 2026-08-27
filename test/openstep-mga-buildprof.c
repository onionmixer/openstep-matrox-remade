/*
 * W5-13 -- profile the CPU trapezoid build, and touch no hardware doing it.
 *
 * The build is 4.38 ms of a 13.18 ms draw phase, measured by arm C minus arm
 * D (docs/W2_WARP_RENDER_PATH_PLAN.md section 19).  It is the largest single
 * term and it is pure host code, so before anything is done about it the
 * question is where inside it the time goes.
 *
 * WHY THIS IS A SEPARATE PROGRAMME AND NOT AN ARM.
 *
 * OpenStepMGAMesaTriangle.c includes stdio, stdlib, string and math and its
 * own header, and nothing else.  No GL context, no driver, no ioctl, no MMIO.
 * So the builder can be linked on its own and driven directly, and this
 * programme does exactly that: it opens no device, maps nothing, and submits
 * nothing.  There is no scissor anywhere in it, which matters because
 * REMAINING_WORK 3-62 forbids scissor together with instrumentation and W5
 * section 13 makes that a condition on this work rather than a hope.
 *
 * The clock is read twice per RUN, never per triangle -- a per-primitive
 * gettimeofday is the other half of that same forbidden pair, and it would
 * also swamp what it is trying to measure: a submission-time pair was
 * measured at 9.16 us against a whole build of 4.38 ms.
 *
 * WHAT IT FEEDS THE BUILDER.
 *
 * Synthetic triangles, and the run says whether they are representative
 * instead of assuming it.  The demo at grid 4 produces, per frame, 978.9
 * source triangles and 1831.8 trapezoids -- 1.871 trapezoids per triangle --
 * over an 800x600 window.  If the generator's trapezoids-per-triangle lands
 * near that, the workload is close enough for the timing to mean something;
 * if it does not, the run says so and the number should not be believed.
 *
 * The generator is deterministic (a fixed LCG seed) so two runs compare.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include "OpenStepMGAMesaTriangle.h"

#define W        800L
#define H        600L
#define SUB      OSMGA_MESA_SUBONE

/* Room for the worst a single triangle can make.  The header says a triangle
 * is at most two trapezoids, and the textured builder adds anchors; sixteen
 * is a margin, not an estimate. */
#define OUTMAX   16

static unsigned long lcgState = 12345UL;

static unsigned long
lcg(void)
{
    /* Numerical Recipes' constants.  Deterministic on purpose: the point is
     * that two runs of this programme see the same triangles. */
    lcgState = lcgState * 1664525UL + 1013904223UL;
    return lcgState;
}

static long
lcgRange(long lo, long hi)
{
    return lo + (long)(lcg() % (unsigned long)(hi - lo + 1L));
}

/*
 * One triangle of roughly `edge` pixels a side, placed so it fits.
 *
 * Not equilateral and not axis-aligned: a builder that splits a triangle into
 * an upper and a lower trapezoid does different work depending on where the
 * middle vertex falls, and a generator that always made the same shape would
 * measure one branch of it.
 */
static void
makeTri(OSMGAMesaVertex *v, long edge)
{
    long ox = lcgRange(0, W - edge - 1);
    long oy = lcgRange(0, H - edge - 1);
    int i;

    for (i = 0; i < 3; i++) {
        v[i].x  = (ox + lcgRange(0, edge)) * SUB + lcgRange(0, SUB - 1);
        v[i].y  = (oy + lcgRange(0, edge)) * SUB + lcgRange(0, SUB - 1);
        v[i].qw = 1.0;
        v[i].tq = 1.0;
        v[i].r  = (unsigned long)lcgRange(0, 255);
        v[i].g  = (unsigned long)lcgRange(0, 255);
        v[i].b  = (unsigned long)lcgRange(0, 255);
        v[i].a  = 255UL;
        v[i].z  = (unsigned long)lcgRange(0, 65535) * 256UL;
        v[i].s  = 0.0;
        v[i].tc = 0.0;
    }
}

static double
nowMs(void)
{
    struct timeval tv;

    gettimeofday(&tv, (struct timezone *)0);
    return (double)tv.tv_sec * 1000.0 + (double)tv.tv_usec / 1000.0;
}

/*
 * One measured run.  The triangles are generated FIRST, into an array, so
 * that the timed region holds the builder and nothing else -- the generator
 * has divides and a modulo in it and would otherwise be counted as build
 * cost.
 */
static void
run(const char *label, long edge, unsigned long tris, unsigned long zmode,
    int depthWrite, int textured)
{
    OSMGAMesaVertex *verts;
    OSMGAHW3DTri *out;
    long tmr[OUTMAX][9];
    OSMGAMesaTex tex;
    unsigned long i, traps = 0UL, refused = 0UL;
    double t0, t1, ms;
    int n;

    /*
     * RESEED PER RUN.  Without this, two runs asking for the same edge see
     * different triangles, because the generator's state carried over -- and
     * the first version of this programme showed edge 8 costing 447 ms in one
     * place and 517 ms in another for what was meant to be identical work.
     * Reseeding makes every row comparable to every other row of the same
     * edge, so a difference between them is the parameter and not the data.
     */
    lcgState = 12345UL;
    verts = (OSMGAMesaVertex *)malloc(sizeof(OSMGAMesaVertex) * 3UL * tris);
    out   = (OSMGAHW3DTri *)malloc(sizeof(OSMGAHW3DTri) * OUTMAX);
    if (verts == 0 || out == 0) {
        printf("buildprof: out of memory\n");
        exit(1);
    }
    for (i = 0UL; i < tris; i++)
        makeTri(&verts[i * 3UL], edge);

    tex.w = 64UL;
    tex.h = 64UL;

    t0 = nowMs();
    for (i = 0UL; i < tris; i++) {
        const OSMGAMesaVertex *v = &verts[i * 3UL];

        if (textured)
            n = OSMGAMesaBuildTriangleTex(&v[0], &v[1], &v[2], &v[0],
                                          zmode, depthWrite,
                                          OSMGA_MESA_BLEND_OPAQUE,
                                          &tex, 0.0, out, tmr);
        else
            n = OSMGAMesaBuildTriangle(&v[0], &v[1], &v[2], &v[0],
                                       zmode, depthWrite,
                                       OSMGA_MESA_BLEND_OPAQUE, 0.0, out);
        if (n < 0) refused++;
        else       traps += (unsigned long)n;
    }
    t1 = nowMs();

    ms = t1 - t0;
    printf("  %-26s edge %4ld  tris %6lu  traps %7lu  %.3f/tri  "
           "refused %4lu  %8.3f ms  %7.3f us/tri  %7.3f us/trap\n",
           label, edge, tris, traps,
           tris ? (double)traps / (double)tris : 0.0,
           refused, ms,
           tris  ? ms * 1000.0 / (double)tris  : 0.0,
           traps ? ms * 1000.0 / (double)traps : 0.0);

    free(verts);
    free(out);
}

int
main(int argc, char **argv)
{
    unsigned long tris = 200000UL;

    if (argc > 1) tris = (unsigned long)atol(argv[1]);

    printf("buildprof: the CPU trapezoid build, no device opened, "
           "no submission made\n");
    printf("buildprof: %lu triangles per run, clock read twice per run\n\n",
           tris);

    printf("A. does the synthetic workload look like the demo?\n");
    printf("   the demo at grid 4: 1.871 trapezoids per source triangle\n");
    run("depth LT, write", 8L, tris, OSMGA_MESA_ZMODE_LT, 1, 0);

    printf("\nB. the same work with pieces removed -- what does each cost?\n");
    run("no depth at all",  8L, tris, OSMGA_MESA_ZMODE_NONE, 0, 0);
    run("depth LT, no write", 8L, tris, OSMGA_MESA_ZMODE_LT, 0, 0);
    run("textured",         8L, tris, OSMGA_MESA_ZMODE_LT, 1, 1);

    printf("\nC. per-triangle fixed cost or per-trapezoid cost?\n");
    printf("   the area changes; the triangle COUNT does not\n");
    run("edge 2",   2L, tris, OSMGA_MESA_ZMODE_LT, 1, 0);
    run("edge 4",   4L, tris, OSMGA_MESA_ZMODE_LT, 1, 0);
    run("edge 8",   8L, tris, OSMGA_MESA_ZMODE_LT, 1, 0);
    run("edge 16", 16L, tris, OSMGA_MESA_ZMODE_LT, 1, 0);
    run("edge 32", 32L, tris, OSMGA_MESA_ZMODE_LT, 1, 0);
    run("edge 64", 64L, tris, OSMGA_MESA_ZMODE_LT, 1, 0);

    /*
     * D exists because A and C disagreed by 16% about identical work.
     *
     * With the generator reseeded per run, A's edge 8 and C's edge 8 build the
     * SAME 200000 triangles into the SAME 285246 trapezoids -- and the second
     * one cost 516 ms against the first's 446.  The data is identical, so the
     * difference is position in the programme: each run mallocs and frees
     * about 4.8 MB, and by the seventh the allocator hands back a differently
     * placed array.
     *
     * So this section repeats one row six times and prints the spread.  Any
     * comparison in A, B or C that is smaller than that spread is not a
     * result.  Publishing the spread is the only thing that makes the rest of
     * the table honest.
     */
    printf("\nD. how much does POSITION alone cost?  identical work, six times\n");
    run("repeat 1", 8L, tris, OSMGA_MESA_ZMODE_LT, 1, 0);
    run("repeat 2", 8L, tris, OSMGA_MESA_ZMODE_LT, 1, 0);
    run("repeat 3", 8L, tris, OSMGA_MESA_ZMODE_LT, 1, 0);
    run("repeat 4", 8L, tris, OSMGA_MESA_ZMODE_LT, 1, 0);
    run("repeat 5", 8L, tris, OSMGA_MESA_ZMODE_LT, 1, 0);
    run("repeat 6", 8L, tris, OSMGA_MESA_ZMODE_LT, 1, 0);

    printf("\nbuildprof: done\n");
    return 0;
}
