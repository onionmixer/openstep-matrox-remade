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

#define SUB (double)OSMGA_MESA_SUBONE

static void
vert(OSMGAMesaVertex *v, double x, double y, double s, double t)
{
    memset(v, 0, sizeof *v);
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
    long tmr[4][9];
    int n, i;

    vert(&a, ax, ay, as, at);
    vert(&b, bx, by, bs, bt);
    vert(&c, cx, cy, cs, ct);
    tex.w = tw; tex.h = th;
    memset(tmr, 0, sizeof tmr);

    n = OSMGAMesaBuildTriangleTex(&a, &b, &c, (const OSMGAMesaVertex *)0,
                                  0UL, OSMGA_MESA_BLEND_OPAQUE, &tex, out, tmr);
    printf("# case %s tex %lu %lu n %d\n", name, tw, th, n);
    printf("# v %ld %ld %.9f %.9f\n", a.x, a.y, a.s, a.tc);
    printf("# v %ld %ld %.9f %.9f\n", b.x, b.y, b.s, b.tc);
    printf("# v %ld %ld %.9f %.9f\n", c.x, c.y, c.s, c.tc);
    if (n < 0) { printf("# refused %d\n", n); return; }
    for (i = 0; i < n; i++)
        printf("T %ld %ld %lu %lu   %ld %ld %ld %ld %ld %ld\n",
               out[i].y, out[i].h,
               (unsigned long)(out[i].fxbndry & 0xFFFFUL),
               (unsigned long)((out[i].fxbndry >> 16) & 0xFFFFUL),
               tmr[i][0], tmr[i][1], tmr[i][2], tmr[i][3],
               tmr[i][6], tmr[i][7]);
    for (i = 0; i < n; i++)
        printf("# opcode %lu (6 is textured)\n", out[i].dwgctl & 0xFUL);
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
    /* a coordinate that leaves the permitted range: must be refused */
    one("D", 10.25,  5.0,  0.0,  0.0,
             40.5,  20.75, 40.0, 0.25,
             18.0,  35.5,  0.25, 1.0,   64UL, 64UL);
    return 0;
}
