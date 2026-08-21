/*
 * openstep-mga-hw3d-ar0-probe.m -- does the engine divide by AR0, or by the
 * row count?
 *
 * The whole of the 3-12 fix rests on one thing nobody has measured.  AR0 and
 * AR6 are what an edge's accumulator divides by, and until now every batch
 * this driver ever sent set them equal to the trapezoid's height, so the two
 * were never told apart.  The fix needs them apart: the divisor belongs to
 * the EDGE, and a triangle split at its middle vertex has one edge spanning
 * both halves.
 *
 * So this asks the question on its own, with one trapezoid and nothing else
 * in the way.  If a Mesa frame came out wrong instead, "the engine ignores
 * AR0" and "the arithmetic is wrong" would look the same.
 *
 * The shape is chosen so the two answers cannot be confused.  Twenty rows,
 * the left edge standing still, the right edge given a displacement of 20
 * over a divisor of 40:
 *
 *   divisor honoured : the boundary is ceil(k/2)  -> 10 columns at row 19
 *   divisor ignored  : the boundary is k          -> 19 columns at row 19
 *
 * Nothing is written that the kernel does not already accept: the batch goes
 * through the same validation and the same clip as any other, and it draws
 * into the 3D block, not the screen.
 *
 *   cc -O -Wall -I../hw3d -o /tmp/ar0 openstep-mga-hw3d-ar0-probe.m -lDriver
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
#define STATUS_PARAM    "OSMGAHW3DStatus"
#define CMD_MMAP_BASE   0x40000000UL
#define CMD_MMAP_LEN    ((int)OSMGA_HW3D_BATCH_BYTES)
#define VRAM_BLOCK      (4UL * 1024UL * 1024UL)
#define CLIP_COLS       64UL
#define ROWS            20UL
#define STRIDE_DW       1024UL
#define SENTINEL        0x5A5A5A5AUL

#define EDGE_D          20L        /* the right edge's displacement */
#define EDGE_DIV        40L        /* and the divisor it is given */

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

/* ceil(a/b) for b > 0 and a >= 0, and the walk never asks for less. */
static long
ceilDiv(long a, long b)
{
    return (a <= 0L) ? 0L : ((a + b - 1L) / b);
}

int
main(void)
{
    IODeviceMaster *master;
    IOObjectNumber objNum;
    IOString kind;
    OSMGAHW3DBatch *batch;
    volatile unsigned long *vram;
    caddr_t cmd, win;
    unsigned long row, col;
    unsigned long honoured = 0UL, ignored = 0UL, matchH = 0UL, matchI = 0UL;
    int fd;
    IOReturn r;
    unsigned st[4], n = 4;

    master = [IODeviceMaster new];
    if ([master lookUpByDeviceName:"Display0" objectNumber:&objNum
            deviceKind:&kind] != IO_R_SUCCESS) {
        printf("Display0 not found\n"); return 1;
    }
    if ((fd = open(DEV_PATH, O_RDWR)) < 0) {
        printf("%s will not open -- is \"VRAM Mmap\" set?\n", DEV_PATH);
        return 1;
    }
    if ((cmd = mapDevice(fd, CMD_MMAP_BASE, CMD_MMAP_LEN)) == (caddr_t)-1) {
        printf("the command window will not map\n"); return 1;
    }
    if ((win = mapDevice(fd, VRAM_BLOCK,
                         (int)(ROWS * STRIDE_DW * 4UL))) == (caddr_t)-1) {
        printf("the VRAM window will not map\n"); return 1;
    }
    batch = (OSMGAHW3DBatch *)cmd;
    vram = (volatile unsigned long *)win;

    printf("does the engine divide by AR0/AR6, or by the row count?\n");
    printf("   %lu rows, right edge displacement %ld over divisor %ld\n\n",
           ROWS, EDGE_D, EDGE_DIV);

    for (row = 0UL; row < ROWS; row++)
        for (col = 0UL; col < CLIP_COLS; col++)
            vram[row * STRIDE_DW + col] = SENTINEL;

    memset(batch, 0, sizeof *batch);
    batch->magic = OSMGA_HW3D_MAGIC;
    batch->version = OSMGA_HW3D_VERSION;
    batch->triCount = 1UL;
    batch->state.dstorg = VRAM_BLOCK;
    batch->state.dstWidth  = CLIP_COLS;
    batch->state.dstHeight = ROWS;
    batch->state.dstPitch  = STRIDE_DW;

    batch->tri[0].dwgctl = 0x4UL | (0x7UL << 4);   /* TRAP | atype I */
    batch->tri[0].alphactrl = 0x00000101UL;
    batch->tri[0].y = 0L;
    batch->tri[0].h = (long)ROWS;
    /* left edge: stands still, so every difference is the right edge's */
    batch->tri[0].ar0 = 1L;
    batch->tri[0].ar1 = 0L;
    batch->tri[0].ar2 = 0L;
    /* right edge: displacement 20, divisor 40, error term zero */
    batch->tri[0].ar4 = -EDGE_D;
    batch->tri[0].ar5 = -EDGE_D;
    batch->tri[0].ar6 = EDGE_DIV;
    batch->tri[0].sgn = 0L;                        /* both edges move right */
    batch->tri[0].fxbndry = (0UL << 16) | 0UL;
    batch->tri[0].dr[0] = 200UL << 15;
    batch->tri[0].dr[3] = 100UL << 15;
    batch->tri[0].dr[6] =  50UL << 15;

    r = [master setIntValues:(unsigned *)0 forParameter:SUBMIT_PARAM
                objectNumber:objNum count:0];
    if ([master getIntValues:st forParameter:STATUS_PARAM objectNumber:objNum
            count:&n] == IO_R_SUCCESS)
        printf("   kernel: verdict %u, triangle %u\n", st[0], st[1]);
    if (r == IO_R_UNSUPPORTED) {
        printf("   acceleration is switched off -- nothing was drawn\n");
        return 0;
    }
    if (r != IO_R_SUCCESS) {
        printf("   FAIL -- the probe batch was refused (%d)\n", (int)r);
        return 1;
    }

    printf("\n   row  drawn  if AR0 honoured  if AR0 ignored\n");
    for (row = 0UL; row < ROWS; row++) {
        unsigned long drawn = 0UL;
        long wantH = ceilDiv(EDGE_D * (long)row, EDGE_DIV);
        long wantI = ceilDiv(EDGE_D * (long)row, (long)ROWS);

        for (col = 0UL; col < CLIP_COLS; col++)
            if (vram[row * STRIDE_DW + col] != SENTINEL) drawn++;
        honoured += (unsigned long)wantH;
        ignored  += (unsigned long)wantI;
        if ((long)drawn == wantH) matchH++;
        if ((long)drawn == wantI) matchI++;
        if (row < 4UL || row + 3UL >= ROWS)
            printf("   %3lu  %5lu  %15ld  %14ld\n", row, drawn, wantH, wantI);
    }
    printf("\n   rows matching \"honoured\": %lu of %lu   (%lu pixels expected)\n",
           matchH, ROWS, honoured);
    printf("   rows matching \"ignored\" : %lu of %lu   (%lu pixels expected)\n",
           matchI, ROWS, ignored);
    printf("\n   %s\n",
           (matchH == ROWS) ? "the engine divides by AR0 -- the 3-12 encoding stands"
           : ((matchI == ROWS)
              ? "the engine uses the row count -- the 3-12 encoding does NOT work"
              : "neither prediction fits; read the staircase above"));
    return (matchH == ROWS) ? 0 : 1;
}
