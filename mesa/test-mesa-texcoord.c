/*
 * The texture coordinate registers the builder computes, printed so that an
 * oracle that does NOT share its arithmetic can check them.
 *
 * What is printed per trapezoid is the anchor the builder chose -- its first
 * row and that row's left edge, both of which are observable in the batch it
 * emits -- and the six TMR values.  The oracle solves the plane itself from
 * the three vertices and evaluates it at (left + 1/2, y + 1/2); taking the
 * anchor from the builder is not circular, because the anchor is a property
 * of the trapezoid and not of the formula under test.
 *
 *   cc -O -Wall -I../hw3d -o /tmp/tc test-mesa-texcoord.c OpenStepMGAMesaTriangle.c
 */
#include <stdio.h>
#include <string.h>
#include "OpenStepMGAMesaTriangle.h"

extern int osmgaHW3DValidate(const OSMGAHW3DBatch *, const OSMGAHW3DLimits *,
                             unsigned long *);

#define SUB (double)OSMGA_MESA_SUBONE

/*
 * The texture q of each vertex.  vert() writes 1.0, which is the affine
 * case; a case that wants perspective sets these before calling one().
 * They are globals rather than three more parameters because one() already
 * takes fourteen.
 */
static double g_tq_a = 1.0, g_tq_b = 1.0, g_tq_c = 1.0;

/*
 * The search below runs one() hundreds of thousands of times and wants to
 * hear about only the cases that disagree, so one() reports through these
 * rather than through its printfs.  g_lastn is what the builder returned;
 * g_worst is the first non-zero verdict the validator gave any trapezoid
 * the builder emitted.  A case with g_lastn > 0 and g_worst != 0 is a
 * triangle the builder passed and the kernel then refused, which is the
 * whole object of the exercise.
 */
/*
 * Which of the three E_TEXCOORD sites answered, as recorded by the
 * validator itself under OSMGA_HW3D_TESTSITE: 1 is the trapezoid's
 * texture anchor, 2 the denominator's anchor and slope budget, 3 the
 * conservative bound of a slope times the trapezoid's own box, and 4 the
 * exact reach of a row's two drawn endpoints.  Three and four both end at
 * the same return and mean opposite things: three is a bound that may
 * refuse a triangle whose drawn pixels are all in range, four is a
 * measurement of those pixels saying they are not.
 */
static long g_site = 0;

static int g_quiet = 0;
static int g_lastn = 0;
static int g_worst = 0;

static void
vert(OSMGAMesaVertex *v, double x, double y, double s, double t)
{
    memset(v, 0, sizeof *v);
    /* A zeroed vertex has neither a w nor a texture q, and the
     * builder divides by both.  One each is what "no perspective and
     * no projective texture" means. */
    v->qw = 1.0;
    v->tq = 1.0;
    v->x = (long)(x * SUB + 0.5);
    v->y = (long)(y * SUB + 0.5);
    v->r = v->g = v->b = 255UL; v->a = 255UL;
    v->s = s; v->tc = t;
}

static void
one(const char *name, double ax, double ay, double as, double at,
    double bx, double by, double bs, double bt,
    double cx, double cy, double cs, double ct,
    unsigned long tw, unsigned long th)
{
    OSMGAMesaVertex a, b, c;
    OSMGAHW3DTri out[4];
    OSMGAMesaTex tex;
    OSMGAHW3DTexReach vreach;
    unsigned long vsite;
    OSMGAHW3DTexBand vbands[OSMGA_HW3D_MAX_TRI];
    long tmr[4][9];
    int n, i;

    vert(&a, ax, ay, as, at);
    vert(&b, bx, by, bs, bt);
    vert(&c, cx, cy, cs, ct);
    a.tq = g_tq_a; b.tq = g_tq_b; c.tq = g_tq_c;
    tex.w = tw; tex.h = th;
    memset(tmr, 0, sizeof tmr);

    n = OSMGAMesaBuildTriangleTex(&a, &b, &c, (const OSMGAMesaVertex *)0,
                                  OSMGA_MESA_ZMODE_NONE,
                                  1 /* depth writes; ZMODE_NONE makes it moot */,
                                  OSMGA_MESA_BLEND_OPAQUE, &tex,
                                  0.0 /* no polygon offset */, out, tmr);
    if (!g_quiet) printf("# case %s tex %lu %lu n %d\n", name, tw, th, n);
    if (!g_quiet) printf("# v %ld %ld %.9f %.9f\n", a.x, a.y, a.s, a.tc);
    if (!g_quiet) printf("# v %ld %ld %.9f %.9f\n", b.x, b.y, b.s, b.tc);
    if (!g_quiet) printf("# v %ld %ld %.9f %.9f\n", c.x, c.y, c.s, c.tc);
    g_lastn = n; g_worst = 0; g_site = 0;
    if (n < 0) { if (!g_quiet) printf("# refused %d\n", n); return; }
    for (i = 0; i < n; i++)
        if (!g_quiet) printf("T %ld %ld %lu %lu   %ld %ld %ld %ld %ld %ld\n",
               out[i].y, out[i].h,
               (unsigned long)(out[i].fxbndry & 0xFFFFUL),
               (unsigned long)((out[i].fxbndry >> 16) & 0xFFFFUL),
               tmr[i][0], tmr[i][1], tmr[i][2], tmr[i][3],
               tmr[i][6], tmr[i][7]);
    for (i = 0; i < n; i++)
        if (!g_quiet) printf("# anchor tu0 %ld  tv0 %ld  tq0 %ld  (span %ld, max %ld)\n",
               (long)out[i].tu0, (long)out[i].tv0, (long)out[i].tq0,
               (long)OSMGA_HW3D_TEX_SPAN, (long)OSMGA_HW3D_TEX_COORD_MAX);
    for (i = 0; i < n; i++)
        if (!g_quiet) printf("# opcode %lu (6 is textured)\n", out[i].dwgctl & 0xFUL);
    /*
     * And through the validator, one trapezoid per batch, because tmr[] is
     * batch state.  This is the whole point: the first real textured triangle
     * was refused for a coordinate at a corner of its bounding box where it
     * draws no pixel.
     */
    {
        static OSMGAHW3DBatch batch;
        static OSMGAHW3DLimits lim;
        unsigned long badTri;
        int v;

        lim.pitchBytes = 1024UL * 4UL;
        lim.clipX1 = 255UL; lim.clipY1 = 63UL;
        lim.colourStart = 4UL * 1024UL * 1024UL;
        lim.colourEnd   = 7UL * 1024UL * 1024UL;
        lim.depthStart  = lim.colourStart; lim.depthEnd = lim.colourEnd;
        lim.texStart    = lim.colourStart; lim.texEnd   = lim.colourEnd;
        lim.batchBytes  = OSMGA_HW3D_BATCH_BYTES;
        lim.maxEdgeWalk = OSMGA_HW3D_EDGE_WALK;
        for (i = 0; i < n; i++) {
            memset(&batch, 0, sizeof batch);
            batch.magic = OSMGA_HW3D_MAGIC;
            batch.version = OSMGA_HW3D_VERSION;
            batch.triCount = 1UL;
            batch.state.dstorg = lim.colourStart;
            batch.state.dstWidth = 256UL;
            batch.state.dstHeight = 64UL;
            batch.state.dstPitch = 1024UL;
            batch.state.texorg = lim.colourStart + 2UL * 1024UL * 1024UL;
            batch.state.texW = tw; batch.state.texH = th; batch.state.texPitch = tw;
            batch.state.texFormat = OSMGA_HW3D_TEXFMT_TW32;
            batch.state.tmr[0] = tmr[i][0]; batch.state.tmr[1] = tmr[i][1];
            batch.state.tmr[2] = tmr[i][2]; batch.state.tmr[3] = tmr[i][3];
            batch.tri[0] = out[i];
            badTri = 0UL;
            vsite = 0UL;
            /*
             * Through the SAME entry the kernel uses.  The three-argument
             * wrapper passes a null reach and a null band table, and the
             * validator has a shortcut -- accept without walking the rows --
             * that it can only take when both are null.  The driver's submit
             * path passes both (OpenStepMGAReplacementDisplay.m:6698), so it
             * never takes that shortcut and always walks.  A harness on the
             * wrapper is therefore measuring a path the card never sees.
             */
            v = osmgaHW3DValidateReachSite(&batch, &lim, &badTri,
                                           &vreach, vbands, &vsite);
            if (v != 0 && g_worst == 0) g_site = (long)vsite;
            if (v != 0 && g_worst == 0) g_worst = v;
            if (!g_quiet) printf("# validator trapezoid %d -> %d\n", i, v);
        }
    }
}

int
main(void)
{
    /* splits into two trapezoids, fractional everywhere, all four gradients
     * non-zero, and the two halves have different left edges */
    one("A", 10.25,  5.0,  0.0,  0.0,
             40.5,  20.75, 1.0,  0.25,
             18.0,  35.5,  0.25, 1.0,   64UL, 64UL);
    /* not a power of two in either axis */
    one("B", 10.25,  5.0,  0.0,  0.0,
             40.5,  20.75, 1.0,  0.25,
             18.0,  35.5,  0.25, 1.0,   48UL, 40UL);
    /* gradients running the other way */
    one("C", 10.25,  5.0,  1.0,  1.0,
             40.5,  20.75, 0.0,  0.75,
             18.0,  35.5,  0.75, 0.0,   64UL, 64UL);
    /*
     * The triangle the hardware probe draws, and the one the validator refused
     * before this: its bounding box has a corner above the widest row where a
     * negative dv/dx puts v at -56742, and no pixel is there.  It must be
     * accepted now.
     */
    one("E",  10.25,  5.0,  0.0,  0.0,
             200.5,  20.75, 1.0,  0.25,
              60.0,  55.5,  0.25, 1.0,   64UL, 64UL);
    /* a coordinate that leaves the permitted range: must be refused */
    one("D", 10.25,  5.0,  0.0,  0.0,
             40.5,  20.75, 40.0, 0.25,
             18.0,  35.5,  0.25, 1.0,   64UL, 64UL);

    /*
     * PERSPECTIVE.  The three cases below all carry vertex COORDINATES --
     * s/q, which is what the builder bounds -- of 0, 1 and 0.25, well inside
     * the permitted range.  What varies between them is the texture q at the
     * top vertex.
     *
     * The builder's range check is sound for points INSIDE the triangle: it
     * says so itself, and the argument is that the ratio there is a convex
     * combination of the vertex ratios.  The anchor the builder then emits
     * is at the top row's left edge, which is NOT inside -- outside the
     * triangle one barycentric weight is negative, the combination is no
     * longer convex, and a ratio may leave the interval its vertices span.
     * With q small at that corner it leaves it by a lot, because the
     * interpolated denominator can approach nought a fraction of a pixel
     * outside the edge.
     *
     * P3 is the control: q barely varies, so the anchor is unremarkable.
     * If P1 is built and then refused by the validator while its vertices
     * are all in range, that is the disagreement, reproduced.
     */
    g_tq_a = 0.01; g_tq_b = 1.0; g_tq_c = 1.0;
    one("P1", 10.25,  5.0,  0.0,   0.0,
              40.5,  20.75, 1.0,   0.25,
              18.0,  35.5,  0.25,  1.0,   64UL, 64UL);
    g_tq_a = 0.1;  g_tq_b = 1.0; g_tq_c = 1.0;
    one("P2", 10.25,  5.0,  0.0,   0.0,
              40.5,  20.75, 1.0,   0.25,
              18.0,  35.5,  0.25,  1.0,   64UL, 64UL);
    g_tq_a = 0.5;  g_tq_b = 1.0; g_tq_c = 1.0;
    one("P3", 10.25,  5.0,  0.0,   0.0,
              40.5,  20.75, 1.0,   0.25,
              18.0,  35.5,  0.25,  1.0,   64UL, 64UL);
    g_tq_a = g_tq_b = g_tq_c = 1.0;

    /*
     * THE SEARCH.
     *
     * Everything above is a triangle somebody chose, and choosing them by
     * hand has now failed twice to find the disagreement that GLQuake
     * provokes thousands of times a second.  So stop choosing.
     *
     * Each round draws a triangle at random, gives each vertex a texture
     * COORDINATE inside the permitted band and a texture q of its own, and
     * hands the result to the real builder and then to the real validator.
     * Only the rounds where the two disagree say anything: the builder
     * emitted trapezoids and the kernel refused one.  Vertices are kept
     * inside the band on purpose -- a triangle the builder refuses is the
     * clean fallback, already measured, and not what is being hunted.
     */
    {
        unsigned long seed = 20260830UL;
        long round, found = 0, built = 0;
        double px[3], py[3], pc[3], pd[3], pq[3];
        int k;

#define RND ((seed = seed * 1103515245UL + 12345UL), \
             (double)((seed >> 8) & 0xFFFFUL) / 65536.0)

        for (round = 0; round < 200000L; round++) {
            for (k = 0; k < 3; k++) {
                px[k] = RND * 250.0;
                py[k] = RND * 60.0;
                /* the coordinate, s/q, inside the band the builder allows */
                pc[k] = -0.9 + RND * 8.8;
                pd[k] = -0.9 + RND * 8.8;
                pq[k] = 0.05 + RND * 3.95;
            }
            g_tq_a = pq[0]; g_tq_b = pq[1]; g_tq_c = pq[2];
            g_quiet = 1;
            /* s is the numerator: coordinate times q */
            one("fuzz", px[0], py[0], pc[0] * pq[0], pd[0] * pq[0],
                        px[1], py[1], pc[1] * pq[1], pd[1] * pq[1],
                        px[2], py[2], pc[2] * pq[2], pd[2] * pq[2],
                        64UL, 64UL);
            g_quiet = 0;
            if (g_lastn > 0) built++;
            if (g_lastn > 0 && g_worst != 0) {
                found++;
                printf("=== GAP %ld: builder emitted %d, validator said %d"
                       " at site %ld\n", found, g_lastn, g_worst, g_site);
                printf("=== q %.6f %.6f %.6f\n", pq[0], pq[1], pq[2]);
                printf("=== coord u %.6f %.6f %.6f\n", pc[0], pc[1], pc[2]);
                printf("=== coord v %.6f %.6f %.6f\n", pd[0], pd[1], pd[2]);
                one("GAP", px[0], py[0], pc[0] * pq[0], pd[0] * pq[0],
                           px[1], py[1], pc[1] * pq[1], pd[1] * pq[1],
                           px[2], py[2], pc[2] * pq[2], pd[2] * pq[2],
                           64UL, 64UL);
                if (found >= 3) break;
            }
        }
        printf("=== search: %ld rounds, %ld built, %ld gaps\n",
               round, built, found);

        /*
         * THE CONTROL.  The same search with every q set to one, which is
         * the affine branch -- the one the teapot takes and the one whose
         * clean fallback has already been measured on the card.  If the
         * disagreement is a property of the perspective divide then this
         * loop finds nothing, and the two counts together say which branch
         * to go and fix.
         */
        {
            /*
             * The affine sweep, by coordinate range.  Each row keeps every
             * vertex coordinate inside [lo, hi] -- so the builder's own
             * check passes by construction -- and counts how often the
             * kernel then refuses what the builder emitted.  Reading the
             * rows against each other says whether the disagreement is
             * about the SIZE of the coordinate or about something the
             * builder does at any size.
             */
            static const double lo[] = { 0.0, 0.0, 0.0, 0.0, -0.9 };
            static const double hi[] = { 1.0, 2.0, 4.0, 7.9,  7.9 };
            double oka, gapa;
            long okn, gapn, site1, site2, s31, s32, s33, site4, site5,
                 v13, vother, vex;
            int band;

            for (band = 0; band < 5; band++) {
                double span = hi[band] - lo[band];
                seed = 20260830UL;
                found = 0; built = 0;
                oka = gapa = 0.0; okn = gapn = 0;
                site1 = site2 = s31 = s32 = s33 = site4 = site5 = 0;
                v13 = vother = vex = 0;
                for (round = 0; round < 20000L; round++) {
                    for (k = 0; k < 3; k++) {
                        px[k] = RND * 250.0;
                        py[k] = RND * 60.0;
                        pc[k] = lo[band] + RND * span;
                        pd[k] = lo[band] + RND * span;
                        (void)RND;
                    }
                    g_tq_a = g_tq_b = g_tq_c = 1.0;
                    g_quiet = 1;
                    one("affine", px[0], py[0], pc[0], pd[0],
                                  px[1], py[1], pc[1], pd[1],
                                  px[2], py[2], pc[2], pd[2], 64UL, 64UL);
                    g_quiet = 0;
                    if (g_lastn > 0) {
                        /*
                         * The triangle's area in pixels.  A gradient is a
                         * coordinate span divided by a screen extent, so a
                         * sliver carries a steep one however small its
                         * coordinates are; if the refused triangles are the
                         * thin ones then that, and not the size of the
                         * coordinate, is what the anchor check is reacting
                         * to.  Comparing the two means is the check.
                         */
                        double ar = ((px[1] - px[0]) * (py[2] - py[0]) -
                                     (px[2] - px[0]) * (py[1] - py[0])) / 2.0;
                        if (ar < 0.0) ar = -ar;
                        built++;
                        if (g_worst != 0) {
                            gapa += ar; gapn++; found++;
                            if (g_site == OSMGA_HW3D_TEXSITE_ANCHOR)   site1++;
                            else if (g_site == OSMGA_HW3D_TEXSITE_QBUDGET) site2++;
                            else if (g_site == OSMGA_HW3D_TEXSITE_SLOPEX)  s31++;
                            else if (g_site == OSMGA_HW3D_TEXSITE_SLOPEDY) s32++;
                            else if (g_site == OSMGA_HW3D_TEXSITE_SLOPEVY) s33++;
                            else if (g_site == OSMGA_HW3D_TEXSITE_ROWENDS) site4++;
                            else if (g_site == OSMGA_HW3D_TEXSITE_EMPTYROW) site5++;
                            /*
                             * Not every refusal is a texture coordinate.
                             * Counting the verdicts apart keeps the site
                             * tally honest: a site of nought means some
                             * other check spoke, and saying which one it
                             * was costs a variable.
                             */
                            if (g_worst == 13) v13++;
                            else { vother++; vex = g_worst; }
                        }
                        else              { oka  += ar; okn++; }
                    }
                }
                printf("=== affine coord [%.1f, %.1f]: %ld built, %ld gaps"
                       "  area ok %.2f gap %.2f\n",
                       lo[band], hi[band], built, found,
                       okn ? oka / (double)okn : 0.0,
                       gapn ? gapa / (double)gapn : 0.0);
                printf("===   anchor %ld qbud %ld  slope-x %ld slope-dy %ld"
                       " slope-vy %ld  rowends %ld emptyrow %ld"
                       "  v13 %ld other %ld\n",
                       site1, site2, s31, s32, s33, site4, site5,
                       v13, vother);
                (void)vex;
            }
        }
    }
    return 0;
}
