/*
 * Is a texture coordinate measured from the screen origin, or from the
 * primitive's own left edge?
 *
 * The whole of stage 4b turns on it.  If the origin is the screen, then a
 * textured triangle that does not start at column zero needs a NEGATIVE
 * TMR6 to put texel zero at its own left edge -- and the validator refuses a
 * negative start, which would block every textured triangle away from the
 * left margin.  If the origin is the primitive, TMR6 stays at zero and
 * nothing is blocked.
 *
 * The measurement draws the SAME textured rectangle twice with IDENTICAL
 * texture state, once at columns 0..63 and once at columns 64..127.  The
 * texture holds its own column index in every texel.
 *
 *   screen origin    -> the second rectangle reads texels 64.. which saturate
 *                       at 63, so it is a solid column of 63
 *   primitive origin -> the second rectangle repeats the 0..63 ramp
 *
 * Both are inside the coordinate bound, so neither is refused for a reason
 * that has nothing to do with the question.
 *
 *   cc -O -Wall -I../hw3d -o /tmp/torg openstep-mga-hw3d-texorigin-probe.m -lDriver
 */
#import <objc/objc.h>
#import <driverkit/IODeviceMaster.h>
#import <driverkit/IODevice.h>
#include <stdio.h>
#include <string.h>
#include <mach/mach.h>
#include "OpenStepMGAHW3D.h"

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
#define DIM             64UL
#define DWG_TEX         (0x6UL | (0x7UL << 4))
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

int
main(void)
{
    IODeviceMaster *master;
    IOObjectNumber objNum;
    IOString kind;
    OSMGAHW3DBatch *batch;
    volatile unsigned long *colour, *tex;
    caddr_t cmd, cwin, twin;
    int fd, pass;
    unsigned st[4], n;
    unsigned long step = OSMGA_HW3D_TEX_SPAN / DIM;
    unsigned long r, c;

    master = [IODeviceMaster new];
    if ([master lookUpByDeviceName:"Display0" objectNumber:&objNum
            deviceKind:&kind] != IO_R_SUCCESS) { printf("no Display0\n"); return 1; }
    if ((fd = open(DEV_PATH, O_RDWR)) < 0) { printf("no %s\n", DEV_PATH); return 1; }
    cmd  = mapDevice(fd, CMD_MMAP_BASE, CMD_MMAP_LEN);
    cwin = mapDevice(fd, COLOUR_ORG, (int)(DIM * STRIDE_DW * 4UL));
    twin = mapDevice(fd, TEX_ORG, (int)(DIM * DIM * 4UL));
    if (cmd == (caddr_t)-1 || cwin == (caddr_t)-1 || twin == (caddr_t)-1) {
        printf("a window will not map\n"); return 1;
    }
    batch  = (OSMGAHW3DBatch *)cmd;
    colour = (volatile unsigned long *)cwin;
    tex    = (volatile unsigned long *)twin;

    /* every texel holds its own column, so a pixel names the texel it took */
    for (r = 0UL; r < DIM; r++)
        for (c = 0UL; c < DIM; c++)
            tex[r * DIM + c] = c;
    for (r = 0UL; r < DIM; r++)
        for (c = 0UL; c < 2UL * DIM; c++)
            colour[r * STRIDE_DW + c] = BLANK;

    printf("texture coordinate origin: the same rectangle at two columns\n");
    printf("   texture %lu wide, one texel per pixel (tmr0 = %lu), TMR6 = 0\n\n",
           DIM, step);

    for (pass = 0; pass < 2; pass++) {
        unsigned long x0 = (pass == 0) ? 0UL : DIM;
        OSMGAHW3DTri *t;
        unsigned long first = 0UL, last = 0UL, same = 0UL;

        memset(batch, 0, sizeof *batch);
        batch->magic = OSMGA_HW3D_MAGIC;
        batch->version = OSMGA_HW3D_VERSION;
        batch->triCount = 1UL;
        batch->state.dstorg = COLOUR_ORG;
        batch->state.dstWidth = 2UL * DIM;      /* small, so the reach check
                                                 * cannot be the reason for a
                                                 * refusal */
        batch->state.dstHeight = DIM;
        batch->state.dstPitch = STRIDE_DW;
        batch->state.texorg = TEX_ORG;
        batch->state.texW = DIM;
        batch->state.texH = DIM;
        batch->state.texPitch = DIM;
        batch->state.texFormat = OSMGA_HW3D_TEXFMT_TW32;
        batch->state.tmr[0] = (long)step;
        batch->state.tmr[3] = (long)step;
        batch->state.tmr[8] = 1L << 16;

        t = &batch->tri[0];
        memset(t, 0, sizeof *t);
        t->dwgctl = DWG_TEX;
        t->alphactrl = 0x00000101UL;
        t->y = 0L;
        t->h = (long)DIM;
        t->ar0 = (long)DIM;
        t->ar6 = (long)DIM;
        t->fxbndry = ((x0 + DIM) << 16) | x0;

        {
            unsigned one = 1U;

            (void)[master setIntValues:&one forParameter:SUBMIT_PARAM
                          objectNumber:objNum count:1];
        }
        n = 4;
        (void)[master getIntValues:st forParameter:STATUS_PARAM
                objectNumber:objNum count:&n];

        first = colour[0 * STRIDE_DW + x0];
        last  = colour[0 * STRIDE_DW + x0 + DIM - 1UL];
        for (c = 0UL; c < DIM; c++)
            if (colour[0 * STRIDE_DW + x0 + c] == first) same++;

        printf("   columns %lu..%lu  verdict %u   row 0: first %lu last %lu"
               "   equal-to-first %lu of %lu\n",
               x0, x0 + DIM - 1UL, st[0], first, last, same, DIM);
    }

    printf("\n   a 0..63 ramp in BOTH  -> the origin is the primitive\n");
    printf("   the second solid 63    -> the origin is the screen\n");
    return 0;
}
