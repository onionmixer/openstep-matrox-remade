/*
 * openstep-mga-hw3d-replay-probe.m -- replay the failing batch with nothing
 * else in the path.
 *
 * A triangle whose registers the builder is known to get right came out 83
 * pixels wider than the rule, identically with depth on and off, while the
 * software path matched.  Before any arithmetic is re-derived, the question
 * is where the difference enters: the engine, the batch, or the submission.
 *
 * So the two trapezoids the builder produced are sent here verbatim -- the
 * numbers below were printed from the builder itself, not recomputed -- with
 * no Mesa, no hook and no clipper in the way.
 *
 *   pass 1  trapezoid 0 alone
 *   pass 2  both, in order
 *
 * and the rows of trapezoid 0 are read after each.  If pass 1 already
 * deviates it is the engine or the state the device actually saw; if only
 * pass 2 does, it is interaction between the two; if neither does, the
 * difference lives in the submission path that Mesa uses and this does not.
 *
 * Deliberately NOT the synthetic fixture I first planned: starting both edges
 * in the same column is the very case under suspicion, and a row's pixel
 * count is not its step count because the endpoint conventions are mixed into
 * it.  This replays the real thing instead.
 *
 *   cc -O -Wall -I../hw3d -o /tmp/rp openstep-mga-hw3d-replay-probe.m -lDriver
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
#define DSTW            320UL
#define DSTH            240UL
#define STRIDE_DW       1024UL
#define SENTINEL        0x5A5A5A5AUL

/* Printed from the builder for the triangle (265,10) (313,152) (146,139). */
static const long ty[2]   = {  10L, 139L };
static const long th[2]   = { 129L,  13L };
static const long tleft[2]= { 265L, 152L };
static const long tright[2]={ 265L, 309L };
static const long tsgn[2] = {   2L,   0L };
static const long tar0[2] = { 258L,  26L };
static const long tar1[2] = {-229L,-332L };
static const long tar2[2] = {-238L,-334L };
static const long tar6[2] = { 284L, 284L };
static const long tar4[2] = {  -2L, 110L };
static const long tar5[2] = { -96L, -96L };

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
fillTri(OSMGAHW3DTri *t, int i)
{
    memset(t, 0, sizeof *t);
    t->dwgctl = 0x4UL | (0x7UL << 4);          /* TRAP | atype I */
    t->alphactrl = 0x00000101UL;
    t->y = ty[i]; t->h = th[i];
    t->ar0 = tar0[i]; t->ar1 = tar1[i]; t->ar2 = tar2[i];
    t->ar4 = tar4[i]; t->ar5 = tar5[i]; t->ar6 = tar6[i];
    t->sgn = tsgn[i];
    t->fxbndry = (((unsigned long)tright[i]) << 16) |
                 ((unsigned long)tleft[i] & 0xffffUL);
    t->dr[0] = 200UL << 15; t->dr[3] = 100UL << 15; t->dr[6] = 50UL << 15;
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
    int fd, pass;
    IOReturn r;
    unsigned st[4], n;

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
                         (int)(DSTH * STRIDE_DW * 4UL))) == (caddr_t)-1) {
        printf("the VRAM window will not map\n"); return 1;
    }
    batch = (OSMGAHW3DBatch *)cmd;
    vram = (volatile unsigned long *)win;

    for (pass = 1; pass <= 2; pass++) {
        for (row = 0UL; row < DSTH; row++)
            for (col = 0UL; col < DSTW; col++)
                vram[row * STRIDE_DW + col] = SENTINEL;

        memset(batch, 0, sizeof *batch);
        batch->magic = OSMGA_HW3D_MAGIC;
        batch->version = OSMGA_HW3D_VERSION;
        batch->triCount = (unsigned long)pass;
        batch->state.dstorg = VRAM_BLOCK;
        batch->state.dstWidth  = DSTW;
        batch->state.dstHeight = DSTH;
        batch->state.dstPitch  = STRIDE_DW;
        fillTri(&batch->tri[0], 0);
        if (pass == 2) fillTri(&batch->tri[1], 1);

        r = [master setIntValues:(unsigned *)0 forParameter:SUBMIT_PARAM
                    objectNumber:objNum count:0];
        n = 4;
        if ([master getIntValues:st forParameter:STATUS_PARAM
                objectNumber:objNum count:&n] != IO_R_SUCCESS) st[0] = 999;
        printf("# pass %d: %lu trapezoid(s), submit %d, verdict %u\n",
               pass, (unsigned long)pass, (int)r, st[0]);
        if (r != IO_R_SUCCESS) { printf("#   refused\n"); continue; }

        for (row = 0UL; row < DSTH; row++) {
            long lo = -1, hi = -1;

            for (col = 0UL; col < DSTW; col++)
                if (vram[row * STRIDE_DW + col] != SENTINEL) {
                    if (lo < 0) lo = (long)col;
                    hi = (long)col;
                }
            if (lo >= 0) printf("R %d %lu %ld %ld\n", pass, row, lo, hi);
        }
    }
    return 0;
}
