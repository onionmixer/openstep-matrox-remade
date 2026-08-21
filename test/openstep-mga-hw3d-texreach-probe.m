/*
 * Is the texture-coordinate reach checked over the TRIANGLE or over the whole
 * destination surface?
 *
 * It matters for the Mesa back end, not as a curiosity.  If the check is over
 * the surface, then a textured triangle narrower than an eighth of the surface
 * is refused however correct its own coordinates are, because the validator
 * extrapolates its gradient across every column the surface has.  Most
 * triangles in a real scene are narrower than that.
 *
 * The measurement holds the batch fixed and changes ONE thing: the width the
 * batch declares for its destination.  The triangle, the texture and every
 * TMR value stay exactly the same.  If the verdict changes, the check is over
 * the surface.  Nothing else can explain it.
 *
 *   cc -O -Wall -I../hw3d -o /tmp/reach openstep-mga-hw3d-texreach-probe.m -lDriver
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

static const char *
why(unsigned v)
{
    switch (v) {
    case OSMGA_HW3D_OK:         return "accepted";
    case OSMGA_HW3D_E_TEXORG:   return "texture origin";
    case OSMGA_HW3D_E_TEXSIZE:  return "texture size";
    case OSMGA_HW3D_E_TEXCOORD: return "texture coordinate";
    default:                    return "other";
    }
}

int
main(void)
{
    IODeviceMaster *master;
    IOObjectNumber objNum;
    IOString kind;
    OSMGAHW3DBatch *batch;
    caddr_t cmd;
    int fd;
    unsigned st[4], n;
    unsigned long widths[6];
    unsigned long triW = 32UL;          /* the triangle, unchanged throughout */
    unsigned long step;
    int i;

    master = [IODeviceMaster new];
    if ([master lookUpByDeviceName:"Display0" objectNumber:&objNum
            deviceKind:&kind] != IO_R_SUCCESS) { printf("no Display0\n"); return 1; }
    if ((fd = open(DEV_PATH, O_RDWR)) < 0) { printf("no %s\n", DEV_PATH); return 1; }
    cmd = mapDevice(fd, CMD_MMAP_BASE, CMD_MMAP_LEN);
    if (cmd == (caddr_t)-1) { printf("no command window\n"); return 1; }
    batch = (OSMGAHW3DBatch *)cmd;

    /* one whole texture across a 32-pixel-wide triangle */
    step = OSMGA_HW3D_TEX_SPAN / triW;

    widths[0] = 64UL; widths[1] = 128UL; widths[2] = 256UL;
    widths[3] = 320UL; widths[4] = 512UL; widths[5] = 1024UL;

    printf("texture reach: the same triangle, only the declared surface width "
           "changes\n");
    printf("   triangle %lu wide, one texture across it, tmr0 = %lu "
           "(%lu texels/pixel scaled)\n", triW, step, OSMGA_HW3D_TEX_SPAN / step);
    printf("   the bound is %lu (8 << 20)\n\n", OSMGA_HW3D_TEX_COORD_MAX);
    printf("   %-10s %-14s %-12s %s\n", "surface W", "reach at W-1", "verdict",
           "what it means");

    for (i = 0; i < 6; i++) {
        unsigned long W = widths[i];
        OSMGAHW3DTri *t;
        unsigned long reach;

        memset(batch, 0, sizeof *batch);
        batch->magic = OSMGA_HW3D_MAGIC;
        batch->version = OSMGA_HW3D_VERSION;
        batch->triCount = 1UL;
        batch->state.dstorg = COLOUR_ORG;
        batch->state.dstWidth = W;
        batch->state.dstHeight = 64UL;
        batch->state.dstPitch = STRIDE_DW;
        batch->state.zorg = 0UL;
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
        t->h = 32L;
        t->ar0 = 32L;
        t->ar6 = 32L;
        t->fxbndry = (triW << 16) | 0UL;   /* columns 0..triW-1 */
        t->dr[0] = 200UL << 15;

        n = 1;
        {
            unsigned one = 1U;

            (void)[master setIntValues:&one forParameter:SUBMIT_PARAM
                          objectNumber:objNum count:1];
        }
        n = 4;
        if ([master getIntValues:st forParameter:STATUS_PARAM
                objectNumber:objNum count:&n] != IO_R_SUCCESS) {
            printf("   status unavailable\n"); continue;
        }
        reach = (W - 1UL) * step;
        printf("   %-10lu %-14lu %u (%-14s) %s\n", W, reach, st[0], why(st[0]),
               reach > OSMGA_HW3D_TEX_COORD_MAX ? "past the bound" : "inside");
    }
    printf("\nIf the verdict turns from accepted to refused as the declared\n"
           "width grows while the triangle never changes, the reach is\n"
           "checked over the surface and not over the triangle.\n");
    return 0;
}
