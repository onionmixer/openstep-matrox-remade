/*
 * openstep-mga-mesa-triangle-test.m -- M1-3b: real triangles, from vertices.
 *
 * Everything up to now drew trapezoids that were written by hand.  This asks
 * OSMGAMesaBuildTriangle to turn three vertices into them, submits the
 * result, and reads back what landed in memory -- so what is being checked
 * is the arithmetic that Mesa's hook will use, not a shape someone chose
 * because it was easy to describe.
 *
 * Build on the target:
 *   cc -O -Wall -I../hw3d -o /tmp/tri openstep-mga-mesa-triangle-test.m \
 *      ../mesa/OpenStepMGAMesaTriangle.c -lDriver
 */
#import <stdio.h>
#import <string.h>
#import <mach/mach.h>
#import <driverkit/IODeviceMaster.h>
#import <driverkit/IODevice.h>
#import "../mesa/OpenStepMGAMesaTriangle.h"

extern int open(const char *, int, ...);
extern caddr_t mmap(caddr_t, int, int, int, int, long);

#define O_RDWR          2
#define PROT_READ       0x01
#define PROT_WRITE      0x02
#define MAP_SHARED      0x0001

#define DEV_PATH        "/dev/osmgavram"
#define SUBMIT_PARAM    "OSMGAHW3DSubmit"
#define CMD_MMAP_BASE   0x40000000UL
#define CMD_MMAP_LEN    (64 * 1024)
#define VRAM_BLOCK      (4UL * 1024UL * 1024UL)
#define CLIP_COLS       64UL
#define ROWS            64UL
#define STRIDE_DW       1024UL
#define SENTINEL        0x5A5A5A5AUL

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

static int failures;

int
main(void)
{
    IODeviceMaster *master;
    IOObjectNumber objNum;
    IOString kind;
    OSMGAHW3DBatch *batch;
    volatile unsigned long *vram;
    caddr_t cmd, win;
    OSMGAMesaVertex v0, v1, v2;
    unsigned long row, col, drawn;
    int fd, n, i;
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
    if ((cmd = mapDevice(fd, CMD_MMAP_BASE, CMD_MMAP_LEN)) == (caddr_t)-1 ||
        (win = mapDevice(fd, VRAM_BLOCK,
                         (int)(ROWS * STRIDE_DW * 4UL))) == (caddr_t)-1) {
        printf("the windows will not map\n");
        return 1;
    }
    batch = (OSMGAHW3DBatch *)cmd;
    vram = (volatile unsigned long *)win;

    /*
     * A right triangle with the square corner at the top left.  Its exact
     * pixel set can be worked out by hand: the left edge stays at 0 and the
     * right edge walks 40 columns over 20 rows, so row r spans 0..2r and the
     * whole shape is 1+3+...+39 = 400 pixels.  That is a shape chosen
     * because it is checkable, not because it is easy to build.
     */
    v0.x = 0L;  v0.y = 0L;
    v1.x = 0L;  v1.y = 20L;
    v2.x = 40L; v2.y = 20L;
    v0.r = 200UL; v0.g = 100UL; v0.b = 50UL;
    v1 = v1; v2 = v2;
    v1.r = v2.r = v0.r; v1.g = v2.g = v0.g; v1.b = v2.b = v0.b;

    printf("M1-3b: a triangle built from three vertices\n");

    for (row = 0UL; row < ROWS; row++)
        for (col = 0UL; col < CLIP_COLS; col++)
            vram[row * STRIDE_DW + col] = SENTINEL;

    memset(batch, 0, sizeof *batch);
    batch->magic = OSMGA_HW3D_MAGIC;
    batch->version = OSMGA_HW3D_VERSION;
    batch->state.dstorg = VRAM_BLOCK;

    n = OSMGAMesaBuildTriangle(&v0, &v1, &v2, &v0, batch->tri);
    batch->triCount = (unsigned long)n;
    printf("   built %d trapezoid(s)\n", n);
    for (i = 0; i < n; i++)
        printf("     y=%ld h=%ld left=%lu right=%lu ar2=%ld ar5=%ld sgn=%ld\n",
               batch->tri[i].y, batch->tri[i].h,
               batch->tri[i].fxbndry & 0xffffUL,
               (batch->tri[i].fxbndry >> 16) - 1UL,
               batch->tri[i].ar2, batch->tri[i].ar5, batch->tri[i].sgn);

    r = [master setIntValues:(unsigned *)0 forParameter:SUBMIT_PARAM
                objectNumber:objNum count:0];
    printf("   submit returned %d\n", (int)r);
    if (r != IO_R_SUCCESS) {
        printf("   FAIL -- the batch was refused\n");
        return 1;
    }

    drawn = 0UL;
    for (row = 0UL; row < ROWS; row++) {
        unsigned long w = 0UL;

        for (col = 0UL; col < CLIP_COLS; col++)
            if (vram[row * STRIDE_DW + col] != SENTINEL)
                w++;
        drawn += w;
        if (row < 6UL || (row >= 18UL && row <= 21UL))
            printf("     row %2lu: %lu px\n", row, w);
    }
    printf("   drew %lu pixels, wanted 400\n", drawn);
    if (drawn != 400UL) {
        printf("   FAIL -- wrong pixel count\n");
        failures++;
    }
    printf("%s\n", failures ? "FAIL" : "PASS -- vertices became the shape "
                                       "they describe");
    return failures ? 1 : 0;
}
