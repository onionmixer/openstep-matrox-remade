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
/* The batch only: the driver no longer lets the command list be mapped,
 * because a client able to rewrite it after validation could put anything in
 * front of the engine. */
#define CMD_MMAP_LEN    ((int)OSMGA_HW3D_BATCH_BYTES)
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
     * right edge walks 40 columns over 20 rows, and the span is [left,right)
     * -- the same half-open rule the software rasteriser uses, which is what
     * makes the two agree pixel for pixel.  So row r spans 0..2r-1, row 0 is
     * empty, and the whole shape is 0+2+4+...+38 = 380 pixels.  That is a shape chosen
     * because it is checkable, not because it is easy to build.
     */
    v0.x = 0L;  v0.y = 0L;
    v1.x = 0L;  v1.y = 20L;
    v2.x = 40L; v2.y = 20L;
    v0.r = 200UL; v0.g = 100UL; v0.b = 50UL;
    v1 = v1; v2 = v2;
    v1.r = v2.r = v0.r; v1.g = v2.g = v0.g; v1.b = v2.b = v0.b;

    printf("M1-3b: a triangle built from three vertices\n");

    /*
     * Shapes that must produce nothing at all.  Each of these once produced
     * a trapezoid: a degenerate triangle walked both edges together and the
     * engine drew the line itself, and a coordinate that is a multiple of
     * 65536 survived FXBNDRY's mask as a small number, passing the kernel's
     * column check while naming a pixel nowhere near the screen.
     */
    {
/*
         * The last of these is not like the other four.  They have no area
         * and there is genuinely nothing to draw; that one is outside the
         * range the back end can express, which is a different answer and now
         * has a different value.  Saying "nothing to draw" about it would let
         * a caller drop a triangle that some other path could have drawn.
         */
        static const struct { long ax, ay, bx, by, cx, cy;
                              int want; const char *what; }
        nothing[5] = {
            {  10L, 0L,  10L, 0L,  20L, 10L, 0, "two vertices the same" },
            {  10L, 0L,  10L, 5L,  10L, 10L, 0, "one column wide" },
            {   0L, 0L,  10L, 5L,  20L, 10L, 0, "three on one line" },
            {   5L, 5L,   5L, 5L,   5L,  5L, 0, "one point" },
            { -65536L, 0L, -65536L, 1L, -65536L, 2L,
              OSMGA_MESA_TRI_UNSUPPORTED, "far off screen" }
        };
        OSMGAHW3DTri scratch[2];
        OSMGAMesaVertex p0, p1, p2;
        int i, got;

        p0.r = p1.r = p2.r = 255UL;
        p0.g = p1.g = p2.g = 255UL;
        p0.b = p1.b = p2.b = 255UL;
        for (i = 0; i < 5; i++) {
            p0.x = nothing[i].ax; p0.y = nothing[i].ay;
            p1.x = nothing[i].bx; p1.y = nothing[i].by;
            p2.x = nothing[i].cx; p2.y = nothing[i].cy;
            got = OSMGAMesaBuildTriangle(&p0, &p1, &p2, &p0,
                               OSMGA_MESA_ZMODE_NONE,
                                     OSMGA_MESA_BLEND_OPAQUE, scratch);
            printf("   %-22s -> %2d (wanted %2d)  %s\n",
                   nothing[i].what, got, nothing[i].want,
                   (got == nothing[i].want) ? "ok" : "FAIL");
            if (got != nothing[i].want)
                failures++;
        }
    }


    for (row = 0UL; row < ROWS; row++)
        for (col = 0UL; col < CLIP_COLS; col++)
            vram[row * STRIDE_DW + col] = SENTINEL;

    memset(batch, 0, sizeof *batch);
    batch->magic = OSMGA_HW3D_MAGIC;
    batch->version = OSMGA_HW3D_VERSION;
    batch->state.dstorg = VRAM_BLOCK;
    /* The batch declares what it may touch; the kernel proves that lies
     * inside the window it owns and clips to it. */
    batch->state.dstWidth  = 64UL;
    batch->state.dstHeight = 120UL;
    batch->state.dstPitch  = 1024UL;   /* the display stride, as before */

    n = OSMGAMesaBuildTriangle(&v0, &v1, &v2, &v0,
                               OSMGA_MESA_ZMODE_NONE,
                               OSMGA_MESA_BLEND_OPAQUE, batch->tri);
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
    printf("   drew %lu pixels, wanted 380\n", drawn);
    if (drawn != 380UL) {
        printf("   FAIL -- wrong pixel count\n");
        failures++;
    }

    /*
     * The same shape again, now with a colour at each vertex.  The plane
     * through them is solved by hand for the check: red falls 12.75 a row,
     * green falls 6.375 a column while rising 12.75 a row, blue rises 6.375
     * a column.  Each sample below is that plane evaluated where the pixel
     * is, so a gradient applied to the wrong axis, or counted from the
     * destination's corner instead of the primitive's, moves every one of
     * them.
     */
    v0.r = 255UL; v0.g =   0UL; v0.b =   0UL;
    v1.r =   0UL; v1.g = 255UL; v1.b =   0UL;
    v2.r =   0UL; v2.g =   0UL; v2.b = 255UL;

    printf("   -- and again with a colour at each vertex\n");
    for (row = 0UL; row < ROWS; row++)
        for (col = 0UL; col < CLIP_COLS; col++)
            vram[row * STRIDE_DW + col] = SENTINEL;

    n = OSMGAMesaBuildTriangle(&v0, &v1, &v2, (OSMGAMesaVertex *)0,
                               OSMGA_MESA_ZMODE_NONE,
                               OSMGA_MESA_BLEND_OPAQUE, batch->tri);
    batch->triCount = (unsigned long)n;
    r = [master setIntValues:(unsigned *)0 forParameter:SUBMIT_PARAM
                objectNumber:objNum count:0];
    if (r != IO_R_SUCCESS) {
        printf("   FAIL -- the smooth batch was refused (%d)\n", (int)r);
        return 1;
    }
    {
        /*
         * Sample points inside the shape under the half-open rule.  Three of
         * the old ones -- row 0 at all, and the last column of rows 10 and
         * 19 -- are outside it now, and read the sentinel rather than a
         * colour, which is the rule working rather than failing.
         */
        static const struct { unsigned long x, y, r, g, b; } want[4] = {
            {  0UL,  1UL, 242UL,  12UL,   0UL },
            { 19UL, 10UL, 127UL,   6UL, 121UL },
            {  0UL, 19UL,  12UL, 242UL,   0UL },
            { 37UL, 19UL,  12UL,   6UL, 235UL }
        };
        int i;

        for (i = 0; i < 4; i++) {
            unsigned long px = vram[want[i].y * STRIDE_DW + want[i].x];
            unsigned long gr = (px >> 16) & 0xffUL;
            unsigned long gg = (px >>  8) & 0xffUL;
            unsigned long gb =  px        & 0xffUL;
            long dr = (long)gr - (long)want[i].r;
            long dg = (long)gg - (long)want[i].g;
            long db = (long)gb - (long)want[i].b;
            int ok = (px != SENTINEL) &&
                     dr <= 1L && dr >= -1L &&
                     dg <= 1L && dg >= -1L &&
                     db <= 1L && db >= -1L;

            printf("     (%2lu,%2lu) got %3lu %3lu %3lu  want %3lu %3lu %3lu  %s\n",
                   want[i].x, want[i].y, gr, gg, gb,
                   want[i].r, want[i].g, want[i].b, ok ? "ok" : "FAIL");
            if (!ok)
                failures++;
        }
    }

    printf("%s\n", failures ? "FAIL" : "PASS -- vertices became the shape "
                                       "and the colours they describe");
    return failures ? 1 : 0;
}
