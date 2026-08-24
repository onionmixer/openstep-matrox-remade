/*
 * The builder's texture coordinates, drawn.
 *
 * test-mesa-texcoord.c proves the six TMR values against an oracle; this
 * proves that those values put the right texel on the screen.  It skips the
 * GL layer entirely -- the builder is linked straight in and the batch is
 * submitted by hand -- so it needs neither the chooser nor an uploader, and a
 * failure here is the builder or the engine and nothing else.
 *
 * Each trapezoid goes in a batch of its own, because tmr[] is batch state and
 * the two halves of a split triangle need different starts.
 *
 *   cc -O -Wall -I../hw3d -I../mesa -o /tmp/td \
 *      openstep-mga-mesa-texdraw-probe.m ../mesa/OpenStepMGAMesaTriangle.c -lDriver
 */
#import <objc/objc.h>
#import <driverkit/IODeviceMaster.h>
#import <driverkit/IODevice.h>
#include <stdio.h>
#include <string.h>
#include <mach/mach.h>
#include "OpenStepMGAHW3D.h"
#include "OpenStepMGAMesaTriangle.h"

/*
 * The texture anchors moved from the batch to the trapezoid, and these
 * sections were written when they were the batch's.  Writing every entry is
 * what the old assignment meant: "this coordinate, for whatever this draws".
 */
static void setTU(OSMGAHW3DBatch *bp, long v)
{ unsigned long i_; for (i_ = 0UL; i_ < OSMGA_HW3D_MAX_TRI; i_++) bp->tri[i_].tu0 = v; }
static void setTV(OSMGAHW3DBatch *bp, long v)
{ unsigned long i_; for (i_ = 0UL; i_ < OSMGA_HW3D_MAX_TRI; i_++) bp->tri[i_].tv0 = v; }
static void setTQ(OSMGAHW3DBatch *bp, long v)
{ unsigned long i_; for (i_ = 0UL; i_ < OSMGA_HW3D_MAX_TRI; i_++) bp->tri[i_].tq0 = v; }


extern caddr_t mmap(caddr_t, int, int, int, int, long);
extern int open(const char *, int, ...);

#define O_RDWR 2
#define PROT_READ 0x01
#define PROT_WRITE 0x02
#define MAP_SHARED 0x0001

#define DEV_PATH        "/dev/osmgavram"
#define SUBMIT_PARAM    "OSMGAHW3DSubmit"
#define STATUS_PARAM    "OSMGAHW3DStatus"
#define CMD_MMAP_BASE   0x40000000UL
#define CMD_MMAP_LEN    ((int)OSMGA_HW3D_BATCH_BYTES)
#define COLOUR_ORG      (4UL * 1024UL * 1024UL)
#define TEX_ORG         (6UL * 1024UL * 1024UL)
#define STRIDE_DW       1024UL
#define DSTW            256UL
#define DSTH            64UL
#define BLANK           0x11223344UL

static caddr_t
mapDevice(int fd, unsigned long offset, int len)
{
    vm_address_t addr = 0;

    if (vm_allocate(task_self(), &addr, (vm_size_t)len, TRUE) != KERN_SUCCESS)
        return (caddr_t)-1;
    if ((int)mmap((caddr_t)addr, len, PROT_READ | PROT_WRITE, MAP_SHARED,
                  fd, (long)offset) == -1)
        return (caddr_t)-1;
    return (caddr_t)addr;
}

static void
vert(OSMGAMesaVertex *v, double x, double y, double s, double t)
{
    memset(v, 0, sizeof *v);
    /* A zeroed vertex has neither a w nor a texture q, and the
     * builder divides by both.  One each is what "no perspective and
     * no projective texture" means. */
    v->qw = 1.0;
    v->tq = 1.0;
    v->x = (long)(x * (double)OSMGA_MESA_SUBONE + 0.5);
    v->y = (long)(y * (double)OSMGA_MESA_SUBONE + 0.5);
    v->r = v->g = v->b = 255UL; v->a = 255UL;
    v->s = s; v->tc = t;
}

int
main(int argc, char **argv)
{
    IODeviceMaster *master;
    IOObjectNumber objNum;
    IOString kind;
    OSMGAHW3DBatch *batch;
    volatile unsigned long *colour, *tex;
    caddr_t cmd, cwin, twin;
    int fd, i, n;
    unsigned long r, c, dim = 64UL;
    int only = 0;
    OSMGAMesaVertex va, vb, vc;
    OSMGAHW3DTri out[4];
    OSMGAMesaTex tx;
    long tmr[4][9];

    if (argc > 1) dim = (unsigned long)atoi(argv[1]);
    if (argc > 2) only = atoi(argv[2]);   /* 0 all, 1 u/col, 2 u/row,
                                           * 3 v/col, 4 v/row */

    master = [IODeviceMaster new];
    if ([master lookUpByDeviceName:"Display0" objectNumber:&objNum
            deviceKind:&kind] != IO_R_SUCCESS) { printf("no Display0\n"); return 1; }
    if ((fd = open(DEV_PATH, O_RDWR)) < 0) { printf("no %s\n", DEV_PATH); return 1; }
    cmd  = mapDevice(fd, CMD_MMAP_BASE, CMD_MMAP_LEN);
    cwin = mapDevice(fd, COLOUR_ORG, (int)(DSTH * STRIDE_DW * 4UL));
    twin = mapDevice(fd, TEX_ORG, (int)(dim * dim * 4UL));
    if (cmd == (caddr_t)-1 || cwin == (caddr_t)-1 || twin == (caddr_t)-1) {
        printf("a window will not map\n"); return 1;
    }
    batch  = (OSMGAHW3DBatch *)cmd;
    colour = (volatile unsigned long *)cwin;
    tex    = (volatile unsigned long *)twin;

    for (r = 0UL; r < dim; r++)
        for (c = 0UL; c < dim; c++)
            tex[r * dim + c] = (r << 8) | c;    /* every texel names itself */
    for (r = 0UL; r < DSTH; r++)
        for (c = 0UL; c < STRIDE_DW; c++)
            colour[r * STRIDE_DW + c] = BLANK;

    /* a triangle that splits, with all four gradients non-zero */
    vert(&va,  10.25,  5.0,  0.0,  0.0);
    vert(&vb, 200.5,  20.75, 1.0,  0.25);
    vert(&vc,  60.0,  55.5,  0.25, 1.0);
    tx.w = dim; tx.h = dim;
    memset(tmr, 0, sizeof tmr);
    n = OSMGAMesaBuildTriangleTex(&va, &vb, &vc, (const OSMGAMesaVertex *)0,
                                  0UL, OSMGA_MESA_BLEND_OPAQUE, &tx, out, tmr);
    printf("# tex %lu trapezoids %d\n", dim, n);
    printf("# v %ld %ld %.9f %.9f\n", va.x, va.y, va.s, va.tc);
    printf("# v %ld %ld %.9f %.9f\n", vb.x, vb.y, vb.s, vb.tc);
    printf("# v %ld %ld %.9f %.9f\n", vc.x, vc.y, vc.s, vc.tc);
    if (n <= 0) { printf("# builder said %d\n", n); return 2; }

    for (i = 0; i < n; i++) {
        unsigned st[4], nn = 4;
        unsigned one = 1U;

        memset(batch, 0, sizeof *batch);
        batch->magic = OSMGA_HW3D_MAGIC;
        batch->version = OSMGA_HW3D_VERSION;
        batch->triCount = 1UL;
        batch->state.dstorg = COLOUR_ORG;
        batch->state.dstWidth = DSTW;
        batch->state.dstHeight = DSTH;
        batch->state.dstPitch = STRIDE_DW;
        batch->state.texorg = TEX_ORG;
        batch->state.texW = dim;
        batch->state.texH = dim;
        batch->state.texPitch = dim;
        batch->state.texFormat = OSMGA_HW3D_TEXFMT_TW32;
        batch->state.tmr[0] = tmr[i][0];
        batch->state.tmr[1] = tmr[i][1];
        batch->state.tmr[2] = tmr[i][2];
        batch->state.tmr[3] = tmr[i][3];
        /* The trapezoid carries its own anchors now, so it goes in before
         * anything overrides them rather than after. */
        batch->tri[0] = out[i];
        batch->tri[0].tq0 = 1L << 16;
        /*
         * One increment at a time, with both starts at zero, so that a term
         * that misbehaves shows up alone instead of inside a sum.
         */
        if (only != 0) {
            batch->state.tmr[0] = (only == 1) ? tmr[i][0] : 0L;
            batch->state.tmr[2] = (only == 2) ? tmr[i][2] : 0L;
            batch->state.tmr[1] = (only == 3) ? tmr[i][1] : 0L;
            batch->state.tmr[3] = (only == 4) ? tmr[i][3] : 0L;
            batch->tri[0].tu0 = 0L;
            batch->tri[0].tv0 = 0L;
            /* the row term is negative for u here; give it a positive one so
             * the coordinate stays non-negative */
            if (only == 2) batch->state.tmr[2] = -tmr[i][2];
            /*
             * v's column term is negative, so a zero start would run the
             * coordinate below zero and the batch would be refused for a
             * reason that has nothing to do with the question.  Start it high
             * enough to cover the whole width instead.
             */
            if (only == 3)
                batch->tri[0].tv0 = -tmr[i][1] * (long)DSTW;
        }
        (void)[master setIntValues:&one forParameter:SUBMIT_PARAM
                      objectNumber:objNum count:1];
        (void)[master getIntValues:st forParameter:STATUS_PARAM
                objectNumber:objNum count:&nn];
        printf("# trapezoid %d y %ld h %ld left %lu verdict %u\n", i,
               out[i].y, out[i].h,
               (unsigned long)(out[i].fxbndry & 0xFFFFUL), st[0]);
        printf("# tmr %d %ld %ld %ld %ld %ld %ld\n", i,
               tmr[i][0], tmr[i][1], tmr[i][2], tmr[i][3],
               tmr[i][6], tmr[i][7]);
        if (st[0] != OSMGA_HW3D_OK) return 2;
    }

    for (r = 0UL; r < DSTH; r++)
        for (c = 0UL; c < DSTW; c++) {
            unsigned long p = colour[r * STRIDE_DW + c];

            if (p == BLANK) continue;
            printf("P %lu %lu %lu %lu\n", c, r, p >> 8, p & 0xFFUL);
        }
    return 0;
}
