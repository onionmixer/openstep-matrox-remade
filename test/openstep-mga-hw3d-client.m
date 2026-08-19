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
    /* One band of destination, plus a guard band above it. */
    if ((win = mapDevice(fd, VRAM_BLOCK,
                         (int)((2UL * BAND) * STRIDE_DW * 4UL))) ==
            (caddr_t)-1) {
        printf("the VRAM window will not map\n");
        return 1;
    }
    batch = (OSMGAHW3DBatch *)cmd;
    vram = (volatile unsigned long *)win;

    printf("M1-2b: a userland client submits a batch\n");

    for (row = 0UL; row < 2UL * BAND; row++)
        for (col = 0UL; col < CLIP_COLS; col++)
            vram[row * STRIDE_DW + col] = SENTINEL;

    memset(batch, 0, sizeof *batch);
    batch->magic = OSMGA_HW3D_MAGIC;
    batch->version = OSMGA_HW3D_VERSION;
    batch->triCount = 1;
    batch->state.dstorg = VRAM_BLOCK;
    batch->state.dwgctl = 0x000C7074UL;         /* TRAP | atype I */
    batch->state.alphactrl = 0x00000101UL;      /* opaque replace */
    fillTriangle(&batch->tri[0], 0UL, BAND, 0L, SLOPE, (long)(CLIP_COLS - 1UL), 0L);

    r = [master setIntValues:(unsigned *)0 forParameter:SUBMIT_PARAM
                objectNumber:objNum count:0];
    printf("   submit returned %d\n", (int)r);
    showStatus(master, objNum);
    if (r != IO_R_SUCCESS) {
        printf("   FAIL -- a well-formed batch was refused\n");
        return 1;
    }

    /* The shape the slope describes, counted here rather than trusted. */
    expect = 0UL;
    for (row = 0UL; row < BAND; row++) {
        long l = (long)(SLOPE * (long)row / (long)BAND);
        long left = (l > (long)(CLIP_COLS - 1UL)) ? (long)CLIP_COLS : l;

        expect += CLIP_COLS - (unsigned long)left;
    }
    got = 0UL;
    for (row = 0UL; row < BAND; row++)
        for (col = 0UL; col < CLIP_COLS; col++)
            if (vram[row * STRIDE_DW + col] != SENTINEL) got++;

    wrong = 0UL;
    for (row = BAND; row < 2UL * BAND; row++)
        for (col = 0UL; col < CLIP_COLS; col++)
            if (vram[row * STRIDE_DW + col] != SENTINEL) wrong++;

    printf("   drew %lu pixels, guard band disturbed %lu\n", got, wrong);
    printf("   first row spans: ");
    for (row = 0UL; row < 20UL; row += 5UL) {
        unsigned long n = 0UL;

        for (col = 0UL; col < CLIP_COLS; col++)
            if (vram[row * STRIDE_DW + col] != SENTINEL) n++;
        printf("row%lu=%lu ", row, n);
    }
    printf("\n");

    /* The one thing the validator exists to stop. */
    batch->state.dstorg = 0UL;
    r = [master setIntValues:(unsigned *)0 forParameter:SUBMIT_PARAM
                objectNumber:objNum count:0];
    printf("   destination aimed at the visible framebuffer -> returned %d\n",
           (int)r);
    showStatus(master, objNum);

    if (wrong != 0UL)
        printf("STOP -- the draw escaped its band\n");
    else if (got == 0UL)
        printf("FAIL -- nothing was drawn\n");
    else if (r == IO_R_SUCCESS)
        printf("STOP -- a hostile destination was accepted\n");
    else
        printf("PASS -- a userland batch drew %lu pixels and a hostile one "
               "was refused\n", got);
    (void)close(fd);
    return 0;
}
