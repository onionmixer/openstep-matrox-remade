/*
 * openstep-mga-hw3d-client.m -- M1-2b: a userland client draws a triangle.
 *
 * The client maps the command window, fills the shared batch, and asks the
 * kernel to run it.  It never names a register, never touches MMIO, and
 * cannot say where the clip is -- the kernel decides that, which is why a
 * batch cannot move the walls it is drawn inside.
 *
 * It then maps the VRAM window and reads the result back, so the check is
 * what landed in memory rather than what the kernel reported.
 *
 * Build on the target, alongside the hw3d header:
 *   cc -O -Wall -o /tmp/osmga-hw3d openstep-mga-hw3d-client.m -lDriver
 */
#import <stdio.h>
#import <string.h>
#import <mach/mach.h>
#import <driverkit/IODeviceMaster.h>
#import <driverkit/IODevice.h>
#import "OpenStepMGAHW3D.h"

extern int open(const char *, int, ...);
extern int close(int);
extern caddr_t mmap(caddr_t, int, int, int, int, long);

#define O_RDWR          2
#define PROT_READ       0x01
#define PROT_WRITE      0x02
#define MAP_SHARED      0x0001

#define DEV_PATH        "/dev/osmgavram"
#define SUBMIT_PARAM    "OSMGAHW3DSubmit"
#define STATUS_PARAM    "OSMGAHW3DStatus"

/* Must match the driver. */
#define CMD_MMAP_BASE   0x40000000UL
#define CMD_MMAP_LEN    (64 * 1024)
#define VRAM_BLOCK      (4UL * 1024UL * 1024UL)
#define CLIP_COLS       64UL
#define BAND            20UL
#define SLOPE           40L
#define NTRI            3UL
#define STRIDE_DW       1024UL          /* 1024x768x4 */
#define SENTINEL        0x5A5A5A5AUL

/*
 * 4.2BSD mmap: no MAP_FIXED and no "pick an address".  _smmap checks that
 * the caller already owns the address, then maps over it, so the placeholder
 * has to be allocated first.  Copied from the S4a probe, which established
 * this the hard way.
 */
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
    case OSMGA_HW3D_OK:        return "accepted";
    case OSMGA_HW3D_E_MAGIC:   return "magic";
    case OSMGA_HW3D_E_VERSION: return "version";
    case OSMGA_HW3D_E_COUNT:   return "triangle count";
    case OSMGA_HW3D_E_DSTORG:  return "destination origin";
    case OSMGA_HW3D_E_ZORG:    return "depth origin";
    case OSMGA_HW3D_E_TEXORG:  return "texture origin";
    case OSMGA_HW3D_E_DWGCTL:  return "drawing control";
    case OSMGA_HW3D_E_TRIROW:  return "triangle rows";
    case OSMGA_HW3D_E_TRICOL:  return "triangle columns";
    case OSMGA_HW3D_E_TRISLOPE:return "edge slope";
    default:                   return "unknown";
    }
}

/* The values osmgaStormTrap derives, computed here because the kernel does
 * not compute geometry for us -- that is the point of the split. */
static void
fillTriangle(OSMGAHW3DTri *t, unsigned long y, unsigned long h,
             long left, long dxL, long right, long dxR)
{
    int sdxl = (dxL < 0) ? 1 : 0;
    int sdxr = (dxR < 0) ? 1 : 0;
    long ar2 = sdxl ? dxL : -dxL;
    long ar5 = sdxr ? dxR : -dxR;

    memset(t, 0, sizeof *t);
    /* Masked form: the client says only opcode, access type and z mode. */
    t->dwgctl = 0x4UL | (0x7UL << 4);           /* TRAP | atype I */
    t->alphactrl = 0x00000101UL;                /* opaque replace */
    t->y = (long)y;
    t->h = (long)h;
    t->ar0 = (long)h;
    t->ar1 = ar2;
    t->ar2 = ar2;
    t->ar4 = ar5;
    t->ar5 = ar5;
    t->ar6 = (long)h;
    t->sgn = ((long)sdxl << 1) | ((long)sdxr << 5);
    t->fxbndry = (((unsigned long)(right + 1L)) << 16) |
                 ((unsigned long)left & 0xffffUL);
    t->dr[0] = 200UL << 15;
    t->dr[3] = 100UL << 15;
    t->dr[6] =  50UL << 15;
}

static void
showStatus(IODeviceMaster *m, unsigned objNum)
{
    unsigned st[4], n = 4;

    if ([m getIntValues:st forParameter:STATUS_PARAM objectNumber:objNum
            count:&n] == IO_R_SUCCESS)
        printf("   kernel: verdict %u (%s), triangle %u, list %u dwords, "
               "%u spins\n", st[0], why(st[0]), st[1], st[2], st[3]);
    else
        printf("   kernel: status parameter unavailable\n");
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
    unsigned long expect, got, row, col, wrong;
    int fd;
    IOReturn r;

    master = [IODeviceMaster new];
    if ([master lookUpByDeviceName:"Display0" objectNumber:&objNum
            deviceKind:&kind] != IO_R_SUCCESS) {
        printf("Display0 not found\n");
        return 1;
    }
    if ((fd = open(DEV_PATH, O_RDWR)) < 0) {
        printf("%s will not open -- is \"VRAM Mmap\" set?\n", DEV_PATH);
        return 1;
    }
    if ((cmd = mapDevice(fd, CMD_MMAP_BASE, CMD_MMAP_LEN)) == (caddr_t)-1) {
        printf("the command window will not map\n");
        return 1;
    }
    /* One band per triangle, plus a guard band above them all.  This has
     * to match what the checks below touch: the first version mapped two
     * bands and then wrote four, which faults rather than misbehaving
     * quietly. */
    if ((win = mapDevice(fd, VRAM_BLOCK,
                         (int)((NTRI + 1UL) * BAND * STRIDE_DW * 4UL))) ==
            (caddr_t)-1) {
        printf("the VRAM window will not map\n");
        return 1;
    }
    batch = (OSMGAHW3DBatch *)cmd;
    vram = (volatile unsigned long *)win;

    printf("M1-2b/2d: a userland client submits a batch of %lu triangles\n",
           NTRI);

    for (row = 0UL; row < (NTRI + 1UL) * BAND; row++)
        for (col = 0UL; col < CLIP_COLS; col++)
            vram[row * STRIDE_DW + col] = SENTINEL;

    memset(batch, 0, sizeof *batch);
    batch->magic = OSMGA_HW3D_MAGIC;
    batch->version = OSMGA_HW3D_VERSION;
    batch->triCount = NTRI;
    batch->state.dstorg = VRAM_BLOCK;

    /* Three different shapes, so a per-triangle loop that reused one
     * triangle's values, or applied them in the wrong order, would show
     * up as the wrong shape in a specific band rather than as a count. */
    fillTriangle(&batch->tri[0], 0UL, BAND,
                 0L, SLOPE, (long)(CLIP_COLS - 1UL), 0L);      /* narrows from the left */
    fillTriangle(&batch->tri[1], BAND, BAND,
                 0L, 0L, (long)(CLIP_COLS - 1UL), 0L);         /* full rectangle */
    fillTriangle(&batch->tri[2], 2UL * BAND, BAND,
                 0L, 0L, (long)(CLIP_COLS - 1UL), -SLOPE);     /* narrows from the right */

    r = [master setIntValues:(unsigned *)0 forParameter:SUBMIT_PARAM
                objectNumber:objNum count:0];
    printf("   submit returned %d\n", (int)r);
    showStatus(master, objNum);
    if (r == IO_R_UNSUPPORTED) {
        /* Not a failure.  The driver refuses every client submission while
         * "Mesa Acceleration" is No, which is the switch doing its job; a
         * test that called this a failure would train us to ignore it. */
        printf("   acceleration is switched off -- refused, as it should be\n");
        return 0;
    }
    if (r != IO_R_SUCCESS) {
        printf("   FAIL -- a well-formed batch of %lu was refused\n", NTRI);
        return 1;
    }

    /* Count each band against its own prediction, not against a total:
     * three shapes summing correctly while individually wrong is exactly
     * what a total would hide. */
    {
        unsigned long want[NTRI], have[NTRI], t, bad = 0UL;

        for (t = 0UL; t < NTRI; t++) {
            want[t] = 0UL;
            for (row = 0UL; row < BAND; row++) {
                long d = (long)(SLOPE * (long)row / (long)BAND);

                if (t == 1UL) want[t] += CLIP_COLS;
                else          want[t] += CLIP_COLS - (unsigned long)d;
            }
            have[t] = 0UL;
            for (row = t * BAND; row < (t + 1UL) * BAND; row++)
                for (col = 0UL; col < CLIP_COLS; col++)
                    if (vram[row * STRIDE_DW + col] != SENTINEL) have[t]++;
            printf("   triangle %lu: wanted %lu pixels, got %lu%s\n",
                   t, want[t], have[t], (want[t] == have[t]) ? "" : "   <-- WRONG");
            if (want[t] != have[t]) bad++;
        }

        /* Row 0 of each band says which direction each edge moved, which a
         * pixel count alone cannot: the first and third shapes have equal
         * areas and are mirror images. */
        for (t = 0UL; t < NTRI; t++) {
            unsigned long first = CLIP_COLS, last = 0UL, mid = t * BAND + 10UL;

            for (col = 0UL; col < CLIP_COLS; col++)
                if (vram[mid * STRIDE_DW + col] != SENTINEL) {
                    if (first == CLIP_COLS) first = col;
                    last = col;
                }
            printf("   triangle %lu row 10 spans x=%lu..%lu\n", t, first, last);
        }

        wrong = 0UL;
        for (row = NTRI * BAND; row < (NTRI + 1UL) * BAND; row++)
            for (col = 0UL; col < CLIP_COLS; col++)
                if (vram[row * STRIDE_DW + col] != SENTINEL) wrong++;
        printf("   guard band disturbed %lu\n", wrong);

        /* A bad triangle in the middle: the verdict has to name it. */
        batch->tri[1].y = (long)(120UL + 1UL);
        r = [master setIntValues:(unsigned *)0 forParameter:SUBMIT_PARAM
                    objectNumber:objNum count:0];
        printf("   triangle 1 put past the clip -> returned %d\n", (int)r);
        showStatus(master, objNum);
        got = (r == IO_R_SUCCESS) ? 1UL : 0UL;

        /* And the one thing the validator exists to stop. */
        batch->tri[1].y = (long)BAND;
        batch->state.dstorg = 0UL;
        expect = [master setIntValues:(unsigned *)0 forParameter:SUBMIT_PARAM
                         objectNumber:objNum count:0] == IO_R_SUCCESS ? 1UL : 0UL;
        printf("   destination aimed at the visible framebuffer -> %s\n",
               expect ? "ACCEPTED" : "refused");
        showStatus(master, objNum);

        if (wrong != 0UL)
            printf("STOP -- a draw escaped its band\n");
        else if (bad != 0UL)
            printf("FAIL -- %lu of %lu triangles drew the wrong shape\n",
                   bad, NTRI);
        else if (got != 0UL)
            printf("STOP -- a batch with a triangle past the clip was accepted\n");
        else if (expect != 0UL)
            printf("STOP -- a hostile destination was accepted\n");
        else
            printf("PASS -- %lu triangles in one batch each drew their own "
                   "shape, and both bad batches were refused\n", NTRI);
    }
    (void)close(fd);
    return 0;
}
