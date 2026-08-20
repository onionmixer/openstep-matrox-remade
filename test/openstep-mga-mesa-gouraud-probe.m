/*
 * openstep-mga-mesa-gouraud-probe.m -- M1-3b-2: where do the colour
 * gradients start counting from?
 *
 * The increments are known: DR6 is red per column, DR11 green per row, and
 * both are in the same (component << 15) scale the start values use.  What
 * is not known is the origin -- whether the start value applies at the
 * destination's own column 0 and row 0, or at the primitive's first column
 * and first row.  Those differ by exactly the primitive's offset, so a
 * trapezoid deliberately placed away from the corner tells them apart.
 *
 * The trapezoid sits at row 8, column 16.  Red rises 255 over 64 columns and
 * green 255 over 20 rows, so at its first pixel the two readings predict
 * (63, 102) if the origin is the destination and (0, 0) if it is the
 * primitive.  Nothing in between is possible.
 *
 * Build on the target:
 *   cc -O -Wall -I../hw3d -o /tmp/gour openstep-mga-mesa-gouraud-probe.m -lDriver
 */
#import <stdio.h>
#import <string.h>
#import <mach/mach.h>
#import <driverkit/IODeviceMaster.h>
#import <driverkit/IODevice.h>
#import "OpenStepMGAHW3D.h"

extern int open(const char *, int, ...);
extern caddr_t mmap(caddr_t, int, int, int, int, long);

#define O_RDWR          2
#define PROT_READ       0x01
#define PROT_WRITE      0x02
#define MAP_SHARED      0x0001

#define DEV_PATH        "/dev/osmgavram"
#define SUBMIT_PARAM    "OSMGAHW3DSubmit"
#define CMD_MMAP_BASE   0x40000000UL
/* The batch only: the driver no longer lets the command list be mapped,
 * because a client able to rewrite it after validation could put anything in
 * front of the engine. */
#define CMD_MMAP_LEN    ((int)OSMGA_HW3D_BATCH_BYTES)
#define VRAM_BLOCK      (4UL * 1024UL * 1024UL)
#define STRIDE_DW       1024UL
#define SENTINEL        0x5A5A5A5AUL

#define TRI_X           16L
#define TRI_Y           8L
#define TRI_W           48L      /* columns 16..63 */
#define TRI_H           20L
#define ROWS            40UL
#define COLS            64UL

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
    volatile unsigned long *vram;
    OSMGAHW3DTri *t;
    caddr_t cmd, win;
    unsigned long row, col, px;
    int fd;
    IOReturn r;

    master = [IODeviceMaster new];
    if ([master lookUpByDeviceName:"Display0" objectNumber:&objNum
            deviceKind:&kind] != IO_R_SUCCESS) {
        printf("Display0 not found\n");
        return 1;
    }
    if ((fd = open(DEV_PATH, O_RDWR)) < 0) {
        printf("%s will not open\n", DEV_PATH);
        return 1;
    }
    if ((cmd = mapDevice(fd, CMD_MMAP_BASE, CMD_MMAP_LEN)) == (caddr_t)-1 ||
        (win = mapDevice(fd, VRAM_BLOCK,
                         (int)(ROWS * STRIDE_DW * 4UL))) == (caddr_t)-1) {
        printf("the windows will not map\n");
        return 1;
    }
    batch = (OSMGAHW3DBatch *)cmd;
    vram = (volatile unsigned long *)win;

    for (row = 0UL; row < ROWS; row++)
        for (col = 0UL; col < COLS; col++)
            vram[row * STRIDE_DW + col] = SENTINEL;

    memset(batch, 0, sizeof *batch);
    batch->magic = OSMGA_HW3D_MAGIC;
    batch->version = OSMGA_HW3D_VERSION;
    batch->triCount = 1UL;
    batch->state.dstorg = VRAM_BLOCK;
    /* The batch declares what it may touch; the kernel proves that lies
     * inside the window it owns and clips to it. */
    batch->state.dstWidth  = 64UL;
    batch->state.dstHeight = 120UL;
    batch->state.dstPitch  = 1024UL;   /* the display stride, as before */

    t = &batch->tri[0];
    t->dwgctl = 0x4UL | (0x7UL << 4);       /* TRAP, access type I */
    t->alphactrl = 0x00000101UL;
    t->y = TRI_Y;
    t->h = TRI_H;
    t->ar0 = TRI_H;
    t->ar6 = TRI_H;
    t->fxbndry = (((unsigned long)(TRI_X + TRI_W)) << 16) |
                 (unsigned long)TRI_X;
    /* red rises across columns, green down rows, blue held so that a
     * component that is simply ignored is distinguishable from one that is
     * zero at the start. */
    t->dr[0] = 0UL;
    t->dr[1] = (255UL << 15) / 64UL;
    t->dr[3] = 0UL;
    t->dr[5] = (255UL << 15) / (unsigned long)TRI_H;
    t->dr[6] = 128UL << 15;

    r = [master setIntValues:(unsigned *)0 forParameter:SUBMIT_PARAM
                objectNumber:objNum count:0];
    printf("M1-3b-2: gradient origin probe -- submit returned %d\n", (int)r);
    if (r != IO_R_SUCCESS)
        return 1;

    printf("   sample                 R    G    B\n");
    {
        struct { unsigned long x, y; } pts[5];
        int i;

        pts[0].x = 16UL; pts[0].y = 8UL;      /* the primitive's first pixel */
        pts[1].x = 32UL; pts[1].y = 8UL;
        pts[2].x = 63UL; pts[2].y = 8UL;
        pts[3].x = 16UL; pts[3].y = 16UL;
        pts[4].x = 16UL; pts[4].y = 27UL;     /* its last row */
        for (i = 0; i < 5; i++) {
            px = vram[pts[i].y * STRIDE_DW + pts[i].x];
            printf("   x=%2lu y=%2lu           %4lu %4lu %4lu%s\n",
                   pts[i].x, pts[i].y,
                   (px >> 16) & 0xffUL, (px >> 8) & 0xffUL, px & 0xffUL,
                   (px == SENTINEL) ? "   (NOT DRAWN)" : "");
        }
    }
    printf("   destination origin predicts  63 102 at the first pixel\n");
    printf("   primitive   origin predicts   0   0 at the first pixel\n");
    return 0;
}
