/*
 * openstep-mga-hw3d-edge-rule-probe.m -- what does AR1 actually mean?
 *
 * Every batch this driver has ever sent, and the D3-2 experiment that fitted
 * the edge walk, set AR1 equal to AR2.  Under that constraint two quite
 * different recurrences emit identical pixels, so the fit could not have
 * chosen between them, and the encoding built on it drew the wrong thing the
 * moment AR1 and AR2 came apart.
 *
 * Three bands, one submission, three questions.  The left edge stands still
 * and both edges start at column 0, so the pixels drawn on a row ARE that
 * row's displacement -- no arithmetic between the readback and the answer.
 *
 * Band 0 separates the three candidates that survive every measurement so
 * far.  With AR0 = 20, AR1 = -20, AR2 = -7 they predict:
 *
 *   drain while a < 0                  0 1 2 2 3 3 3 4
 *   drain while a <= 0                 0 2 2 2 3 3 3 4
 *   the old rule plus a startup carry  0 1 2 2 2 3 3 3
 *
 * Band 1 is the control that has been measured twice already (AR1 == AR2),
 * where all three agree -- if it disagrees, the probe itself is wrong.
 * Band 2 is the zero-displacement case the quad exposed.
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
#define BAND            20UL
#define NBAND           3UL
#define STRIDE_DW       1024UL
#define SENTINEL        0x5A5A5A5AUL

static const char *bandName[NBAND] = {
    "AR1 != AR2, the case that separates the candidates",
    "AR1 == AR2, the control measured twice before",
    "zero displacement, the case the quad exposed"
};
static const long bandAR0[NBAND] = {  20L,  40L, 280L };
static const long bandAR1[NBAND] = { -20L, -20L, -140L };
static const long bandAR2[NBAND] = {  -7L, -20L,   0L };
static const long bandRows[NBAND] = {  8L,  20L,  20L };

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

static long
ceilDiv(long a, long b)                 /* b > 0 */
{
    return (a >= 0L) ? ((a + b - 1L) / b) : -((-a) / b);
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
    unsigned long row, col, band;
    int fd;
    IOReturn r;
    unsigned st[4], n = 4;

    master = [IODeviceMaster new];
    if ([master lookUpByDeviceName:"Display0" objectNumber:&objNum
            deviceKind:&kind] != IO_R_SUCCESS) {
        printf("Display0 not found\n"); return 1;
    }
    if ((fd = open(DEV_PATH, O_RDWR)) < 0) {
        printf("%s will not open\n", DEV_PATH); return 1;
    }
    if ((cmd = mapDevice(fd, CMD_MMAP_BASE, CMD_MMAP_LEN)) == (caddr_t)-1) {
        printf("the command window will not map\n"); return 1;
    }
    if ((win = mapDevice(fd, VRAM_BLOCK,
                         (int)(NBAND * BAND * STRIDE_DW * 4UL))) == (caddr_t)-1) {
        printf("the VRAM window will not map\n"); return 1;
    }
    batch = (OSMGAHW3DBatch *)cmd;
    vram = (volatile unsigned long *)win;

    for (row = 0UL; row < NBAND * BAND; row++)
        for (col = 0UL; col < CLIP_COLS; col++)
            vram[row * STRIDE_DW + col] = SENTINEL;

    memset(batch, 0, sizeof *batch);
    batch->magic = OSMGA_HW3D_MAGIC;
    batch->version = OSMGA_HW3D_VERSION;
    batch->triCount = NBAND;
    batch->state.dstorg = VRAM_BLOCK;
    batch->state.dstWidth  = CLIP_COLS;
    batch->state.dstHeight = NBAND * BAND;
    batch->state.dstPitch  = STRIDE_DW;

    for (band = 0UL; band < NBAND; band++) {
        OSMGAHW3DTri *t = &batch->tri[band];

        memset(t, 0, sizeof *t);
        t->dwgctl = 0x4UL | (0x7UL << 4);
        t->alphactrl = 0x00000101UL;
        t->y = (long)(band * BAND);
        t->h = bandRows[band];
        t->ar0 = 1L; t->ar1 = 0L; t->ar2 = 0L;      /* left: stands still */
        t->ar6 = bandAR0[band];
        t->ar4 = bandAR1[band];
        t->ar5 = bandAR2[band];
        t->sgn = 0L;
        t->fxbndry = (0UL << 16) | 0UL;
        t->dr[0] = 200UL << 15; t->dr[3] = 100UL << 15; t->dr[6] = 50UL << 15;
    }

    r = [master setIntValues:(unsigned *)0 forParameter:SUBMIT_PARAM
                objectNumber:objNum count:0];
    if ([master getIntValues:st forParameter:STATUS_PARAM objectNumber:objNum
            count:&n] == IO_R_SUCCESS)
        printf("kernel: verdict %u, triangle %u\n\n", st[0], st[1]);
    if (r != IO_R_SUCCESS) {
        printf("the probe batch was refused (%d)\n", (int)r); return 1;
    }

    for (band = 0UL; band < NBAND; band++) {
        long mag = -bandAR2[band];
        long e = -bandAR1[band] - mag;
        long dy = bandAR0[band];
        long okA = 0L, okB = 0L, okC = 0L, k;

        printf("band %lu: %s\n", band, bandName[band]);
        printf("   AR0=%ld AR1=%ld AR2=%ld  (magnitude %ld, error term %ld)\n",
               bandAR0[band], bandAR1[band], bandAR2[band], mag, e);
        printf("   row  drawn   a<0   a<=0   old+carry\n");
        for (k = 0L; k < bandRows[band]; k++) {
            unsigned long drawn = 0UL;
            long a = (k == 0L) ? 0L : ceilDiv(mag * k + e, dy);
            long b = (k == 0L) ? 0L : ((mag * k + e) / dy + 1L);
            long c = ceilDiv(mag * k - e, dy) + ((k >= 1L) ? 1L : 0L);

            if (a < 0L) a = 0L;
            if (b < 0L) b = 0L;
            if (c < 0L) c = 0L;
            for (col = 0UL; col < CLIP_COLS; col++)
                if (vram[(band * BAND + (unsigned long)k) * STRIDE_DW + col]
                        != SENTINEL) drawn++;
            okA += ((long)drawn == a); okB += ((long)drawn == b);
            okC += ((long)drawn == c);
            if (k < 8L)
                printf("   %3ld  %5lu  %4ld  %5ld  %10ld\n", k, drawn, a, b, c);
        }
        printf("   matched: a<0 %ld/%ld,  a<=0 %ld/%ld,  old+carry %ld/%ld\n\n",
               okA, bandRows[band], okB, bandRows[band], okC, bandRows[band]);
    }
    return 0;
}
