/*
 * Does the engine DRAW at the pitch the batch declared?
 *
 * That it ACCEPTS an unusual pitch was settled long ago; that it draws at one
 * was not, because the client that settled acceptance still indexed its
 * readback at the display's 1024 and so its pixel counts meant nothing
 * (REMAINING_WORK 3-24).  "The validator passes 992 and the register gets
 * 1024" is a regression only this can catch.
 *
 * The shape of the test is the whole point: fill EVERY word of the mapped
 * window with a sentinel, draw, and then require that the set of words which
 * CHANGED is exactly the set the geometry names.  Comparing sets rather than
 * counts is what makes it immune to where a wrong stride happens to land --
 * a pitch error, a coordinate slip and an overrun all fail it, and none of
 * them can hide behind a matching number of lit pixels.
 *
 * The window is scanned in full, at flat word offsets, so a row written at
 * the WRONG stride is inside what is looked at rather than past the end.
 *
 *   cc -O -Wall -I../hw3d -o tpq openstep-mga-hw3d-pitch-client.m -lDriver
 */
#import <stdio.h>
#import <string.h>
#import <mach/mach.h>
#import <driverkit/IODeviceMaster.h>
#import <driverkit/IODevice.h>
#import "OpenStepMGAHW3D.h"

extern int open(const char *, int, ...);
extern caddr_t mmap(caddr_t, int, int, int, int, long);

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

/*
 * 992 is legal: a multiple of 32, and no wider than the display's stride --
 * which is a CEILING here, not a substitute (the submit path refuses a pitch
 * above it and programs the batch's own).
 *
 * The width is deliberately less than the pitch, so there is real padding
 * between rows, and the shape is put hard against the right edge of that
 * width.  Then a row drawn at 1024 instead of 992 slips by exactly the bar's
 * width and lands in the padding -- a place the test says nothing may write.
 */
#define PITCH           992UL
#define WIDTH           900UL
#define ROWS            16UL
#define BARW            32UL            /* 1024 - 992 */
#define BARX            (WIDTH - BARW)  /* 868 */

/* the whole window this scans, in words: comfortably past 15*1024 + 900 */
#define WORDS           32768UL
#define SENTINEL        0x5A5A5A5AUL
#define DRAWN           0x00C0FFEEUL

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

static IODeviceMaster *master;
static IOObjectNumber objNum;
static OSMGAHW3DBatch *batch;
static volatile unsigned long *colour;

static unsigned
verdict(void)
{
    unsigned st[4], n = 4;

    if ([master getIntValues:st forParameter:STATUS_PARAM objectNumber:objNum
            count:&n] != IO_R_SUCCESS)
        return 0xFFFFU;
    return st[0];
}

/*
 * One axis-aligned bar, ROWS tall, at the right edge of the declared width.
 * dr[0] is the colour the trapezoid lays down.
 */
static void
bar(OSMGAHW3DTri *t)
{
    memset(t, 0, sizeof *t);
    t->dwgctl = 0x4UL | (0x7UL << 4);       /* TRAP, access type I */
    t->alphactrl = 0x00000101UL;
    t->y = 0L;
    t->h = (long)ROWS;
    t->ar0 = (long)ROWS;                    /* axis aligned: no slope */
    t->ar6 = (long)ROWS;
    t->fxbndry = ((BARX + BARW) << 16) | BARX;
    t->dr[0] = (DRAWN & 0xFFUL) << 15;          /* blue  */
    t->dr[3] = ((DRAWN >> 8) & 0xFFUL) << 15;   /* green */
    t->dr[6] = ((DRAWN >> 16) & 0xFFUL) << 15;  /* red   */
}

int
main(void)
{
    IOString kind;
    caddr_t cmd, cwin;
    int fd;
    unsigned long i, changed = 0UL, wrong = 0UL, missing = 0UL;
    unsigned long r, c;
    unsigned v;
    unsigned long guard;

    master = [IODeviceMaster new];
    if ([master lookUpByDeviceName:"Display0" objectNumber:&objNum
            deviceKind:&kind] != IO_R_SUCCESS) {
        printf("Display0 not found\n"); return 1;
    }
    if ((fd = open(DEV_PATH, O_RDWR)) < 0) {
        printf("%s will not open\n", DEV_PATH); return 1;
    }
    cmd  = mapDevice(fd, CMD_MMAP_BASE, CMD_MMAP_LEN);
    cwin = mapDevice(fd, COLOUR_ORG, (int)(WORDS * 4UL));
    if (cmd == (caddr_t)-1 || cwin == (caddr_t)-1) {
        printf("a window will not map\n"); return 1;
    }
    batch  = (OSMGAHW3DBatch *)cmd;
    colour = (volatile unsigned long *)cwin;

    printf("drawing at a pitch that is not the display's\n\n");
    printf("   pitch %lu, declared width %lu, %lu rows,"
           " bar at columns %lu..%lu\n",
           PITCH, WIDTH, ROWS, BARX, BARX + BARW - 1UL);
    printf("   a row drawn at 1024 instead would slip %lu words, which is"
           " exactly the bar's width\n", 1024UL - PITCH);

    for (i = 0UL; i < WORDS; i++)
        colour[i] = SENTINEL;

    memset(batch, 0, sizeof *batch);
    batch->magic = OSMGA_HW3D_MAGIC;
    batch->version = OSMGA_HW3D_VERSION;
    batch->triCount = 1;
    batch->state.dstorg = COLOUR_ORG;
    batch->state.dstWidth  = WIDTH;
    batch->state.dstHeight = ROWS;
    batch->state.dstPitch  = PITCH;
    bar(&batch->tri[0]);

    if ([master setIntValues:(unsigned *)0 forParameter:SUBMIT_PARAM
                objectNumber:objNum count:0] != IO_R_SUCCESS) {
        v = verdict();
        printf("   the batch was refused (verdict %u)\n", v);
        return 1;
    }

    /*
     * One read past the first 64 bytes before the scan.  Writes the engine
     * makes into that first stretch of the window become visible late unless
     * the last read before looking fell outside it -- measured, and recorded
     * as the settling read.  Without this a word that simply had not appeared
     * yet would read as a pitch failure.
     */
    guard = colour[WORDS - 1UL];
    (void)guard;

    for (i = 0UL; i < WORDS; i++) {
        unsigned long got = colour[i] & 0x00FFFFFFUL;
        int want = 0;

        r = i / PITCH;
        c = i % PITCH;
        if (r < ROWS && c >= BARX && c < BARX + BARW)
            want = 1;

        /*
         * WHERE, not what.  The question this answers is which words the
         * engine touched, and a word is touched when it no longer holds the
         * sentinel -- what colour replaced it is a different test's business
         * and depends on a channel order this one has no need to know.  The
         * first version of this predicted the colour, and failed with the
         * geometry perfectly right: 512 words changed, none outside, and
         * none of them the shade it had guessed.
         */
        if (got != (SENTINEL & 0x00FFFFFFUL)) changed++;
        if (want && got == (SENTINEL & 0x00FFFFFFUL)) missing++;
        if (!want && got != (SENTINEL & 0x00FFFFFFUL)) wrong++;
    }

    printf("\n   words that changed          : %lu\n", changed);
    printf("   intended words left untouched: %lu\n", missing);
    printf("   words written that should not be: %lu\n", wrong);
    printf("   (the bar is %lu words: %lu rows of %lu)\n",
           ROWS * BARW, ROWS, BARW);

    if (missing == 0UL && wrong == 0UL && changed == ROWS * BARW) {
        printf("\n=== the engine draws at the pitch the batch declared ===\n");
        return 0;
    }
    printf("\n=== PROBLEM: the changed set is not the set the geometry"
           " names ===\n");
    return 1;
}
