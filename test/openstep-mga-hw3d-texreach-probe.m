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
 * The first measurement held the batch fixed and changed one thing: the width
 * the batch declares.  The verdict flipped at 320, which settled it.
 *
 * After the fix, "all six widths accepted" is NOT enough to believe: deleting
 * the check entirely would also produce it.  So this now also requires
 *
 *   - the accepted cases to put the RIGHT TEXELS on the screen, read back;
 *   - a violation reached by a pixel that is actually drawn to stay refused;
 *   - a textured triangle with height but no columns to stay refused, since
 *     it is still encoded and still executed;
 *   - a sloped edge, where the widest row is not the first one;
 *   - bilinear filtering, which the earlier texture work never exercised;
 *   - a negative gradient, which is half of all real texture mapping.
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

#define BLANK 0x11223344UL

static IODeviceMaster *master;
static IOObjectNumber objNum;
static OSMGAHW3DBatch *batch;
static volatile unsigned long *colour;
static int failures;

/* submit and report the verdict */
static unsigned
fire(void)
{
    unsigned st[4], n = 4;
    unsigned one = 1U;

    (void)[master setIntValues:&one forParameter:SUBMIT_PARAM
                  objectNumber:objNum count:1];
    if ([master getIntValues:st forParameter:STATUS_PARAM objectNumber:objNum
            count:&n] != IO_R_SUCCESS)
        return 0xFFFFU;
    return st[0];
}

static void
say(const char *what, unsigned got, unsigned want)
{
    if (got == want)
        printf("   ok    %-52s verdict %u\n", what, got);
    else {
        printf("   FAIL  %-52s verdict %u, wanted %u\n", what, got, want);
        failures++;
    }
}

/* a batch with one textured triangle; the caller adjusts and fires */
static OSMGAHW3DTri *
setup(unsigned long dstW, unsigned long x0, unsigned long w, unsigned long h,
      long grad, unsigned long flags)
{
    OSMGAHW3DTri *t;

    memset(batch, 0, sizeof *batch);
    batch->magic = OSMGA_HW3D_MAGIC;
    batch->version = OSMGA_HW3D_VERSION;
    batch->triCount = 1UL;
    batch->state.dstorg = COLOUR_ORG;
    batch->state.dstWidth = dstW;
    batch->state.dstHeight = 64UL;
    batch->state.dstPitch = STRIDE_DW;
    batch->state.texorg = TEX_ORG;
    batch->state.texW = DIM;
    batch->state.texH = DIM;
    batch->state.texPitch = DIM;
    batch->state.texFormat = OSMGA_HW3D_TEXFMT_TW32;
    batch->state.texFlags = flags;
    batch->state.tmr[0] = grad;
    batch->state.tmr[3] = (long)(OSMGA_HW3D_TEX_SPAN / DIM);
    batch->state.tmr[8] = 1L << 16;

    t = &batch->tri[0];
    memset(t, 0, sizeof *t);
    t->dwgctl = DWG_TEX;
    t->alphactrl = 0x00000101UL;
    t->y = 0L;
    t->h = (long)h;
    t->ar0 = (long)h;
    t->ar6 = (long)h;
    t->fxbndry = ((x0 + w) << 16) | x0;
    t->dr[0] = 200UL << 15;
    return t;
}

static void
blank(void)
{
    unsigned long r, c;

    for (r = 0UL; r < 64UL; r++)
        for (c = 0UL; c < STRIDE_DW; c++)
            colour[r * STRIDE_DW + c] = BLANK;
}

int
main(void)
{
    IOString kind;
    caddr_t cmd, cwin, twin;
    volatile unsigned long *tex;
    int fd;
    unsigned long r, c, step, wrong;
    unsigned long widths[6];
    int i;

    master = [IODeviceMaster new];
    if ([master lookUpByDeviceName:"Display0" objectNumber:&objNum
            deviceKind:&kind] != IO_R_SUCCESS) { printf("no Display0\n"); return 1; }
    if ((fd = open(DEV_PATH, O_RDWR)) < 0) { printf("no %s\n", DEV_PATH); return 1; }
    cmd  = mapDevice(fd, CMD_MMAP_BASE, CMD_MMAP_LEN);
    cwin = mapDevice(fd, COLOUR_ORG, (int)(64UL * STRIDE_DW * 4UL));
    twin = mapDevice(fd, TEX_ORG, (int)(DIM * DIM * 4UL));
    if (cmd == (caddr_t)-1 || cwin == (caddr_t)-1 || twin == (caddr_t)-1) {
        printf("a window will not map\n"); return 1;
    }
    batch  = (OSMGAHW3DBatch *)cmd;
    colour = (volatile unsigned long *)cwin;
    tex    = (volatile unsigned long *)twin;

    for (r = 0UL; r < DIM; r++)
        for (c = 0UL; c < DIM; c++)
            /*
             * Both coordinates, not one.  A texture whose rows are all the
             * same cannot tell a right v from a wrong one, so "every pixel
             * took its texel" would have meant "took its column".
             */
            tex[r * DIM + c] = (r << 8) | c;

    step = OSMGA_HW3D_TEX_SPAN / 32UL;          /* one texture across 32 px */

    printf("texture reach after the change\n\n");

    printf("1. the same 32-column triangle, only the declared width changes\n");
    widths[0]=64UL; widths[1]=128UL; widths[2]=256UL;
    widths[3]=320UL; widths[4]=512UL; widths[5]=1024UL;
    for (i = 0; i < 6; i++) {
        char name[64];

        blank();
        (void)setup(widths[i], 0UL, 32UL, 32UL, (long)step, 0UL);
        sprintf(name, "surface %lu wide", widths[i]);
        say(name, fire(), OSMGA_HW3D_OK);
    }

    printf("\n2. and the accepted drawing is the right texels, read back\n");
    blank();
    (void)setup(1024UL, 0UL, 32UL, 32UL, (long)step, 0UL);
    (void)fire();
    wrong = 0UL;
    for (r = 0UL; r < 32UL; r++)
        for (c = 0UL; c < 32UL; c++) {
            /* one texture across 32 columns in x, one texel per row in y */
            unsigned long want = (r << 8) | ((c * DIM) / 32UL);

            if (colour[r * STRIDE_DW + c] != want) wrong++;
        }
    if (wrong == 0UL)
        printf("   ok    %-52s %lu of %lu\n", "every drawn pixel took its texel",
               1024UL - wrong, 1024UL);
    else {
        printf("   FAIL  %-52s %lu wrong\n", "every drawn pixel took its texel",
               wrong);
        /* A count is a summary, not a diagnosis: say WHAT is wrong. */
        for (r = 0UL; r < 3UL; r++) {
            for (c = 0UL; c < 6UL; c++) {
                unsigned long got = colour[r * STRIDE_DW + c];
                unsigned long want = (r << 8) | ((c * DIM) / 32UL);

                printf("         (%lu,%lu) got (v=%lu,u=%lu) want (v=%lu,u=%lu)\n",
                       c, r, got >> 8, got & 0xFFUL, want >> 8, want & 0xFFUL);
            }
        }
        failures++;
    }
    {
        unsigned long spilled = 0UL;

        for (r = 0UL; r < 64UL; r++)
            for (c = 32UL; c < STRIDE_DW; c++)
                if (colour[r * STRIDE_DW + c] != BLANK) spilled++;
        if (spilled == 0UL)
            printf("   ok    %-52s\n", "nothing drawn outside the 32 columns, whole row");
        else {
            printf("   FAIL  %-52s %lu\n", "nothing drawn outside the 32 columns",
                   spilled);
            failures++;
        }
    }

    printf("\n3. a violation an emitted pixel really reaches stays refused\n");
    blank();
    (void)setup(1024UL, 0UL, 320UL, 32UL, (long)step, 0UL);
    say("the same gradient across a 320-column primitive", fire(),
        OSMGA_HW3D_E_TEXCOORD);
    blank();
    /*
     * Seventeen, not nine.  Nine times the identity was what the earlier
     * client used, but that was over 64 columns AND the whole surface; over
     * 31 columns the budget is not spent until sixteen and a half times, so
     * "nine" would have been a case that passes because it is correct, wearing
     * the label of a case that fails.
     */
    (void)setup(1024UL, 0UL, 32UL, 32UL,
                (long)(OSMGA_HW3D_TEX_SPAN / DIM * 17UL), 0UL);
    say("seventeen times the identity gradient on 32 columns", fire(),
        OSMGA_HW3D_E_TEXCOORD);
    blank();
    {
        OSMGAHW3DTri *t = setup(1024UL, 0UL, 32UL, 32UL, (long)step, 0UL);

        batch->state.tmr[6] = -1L;
        say("a negative start", fire(), OSMGA_HW3D_E_TEXCOORD);
        (void)t;
    }

    printf("\n4. height but no columns -- still encoded, so still refused\n");
    blank();
    {
        OSMGAHW3DTri *t = setup(1024UL, 7UL, 0UL, 32UL,
                                (long)(OSMGA_HW3D_TEX_SPAN / DIM * 9UL), 0UL);

        (void)t;
        say("a textured span that draws nothing", fire(),
            OSMGA_HW3D_E_TRIEMPTY);
    }

    printf("\n5. a sloped edge, where the widest row is not the first\n");
    blank();
    {
        OSMGAHW3DTri *t = setup(1024UL, 0UL, 8UL, 32UL, (long)step, 0UL);

        /* the right edge takes one column per row: a = ar4 - ar5 = 31,
         * then a += ar5 each row and steps whenever it goes below zero */
        t->ar6 = 32L;
        t->ar5 = -32L;
        t->ar4 = -1L;
        say("a right edge opening to 8 + 32 columns", fire(), OSMGA_HW3D_OK);
        {
            unsigned long widest = 0UL;

            for (c = 0UL; c < 200UL; c++)
                if (colour[31UL * STRIDE_DW + c] != BLANK) widest = c;
            printf("         last drawn column on the bottom row: %lu\n", widest);
        }
    }
    blank();
    {
        OSMGAHW3DTri *t = setup(1024UL, 0UL, 8UL, 32UL,
                                (long)(OSMGA_HW3D_TEX_SPAN / 4UL), 0UL);

        /*
         * The same slope with a gradient chosen to sit between the two
         * answers: over row 0's seven columns it spends 1835008 of the
         * 8388608 budget and would be accepted, and over the full 38 columns
         * it spends 9961472 and must not be.  A gradient outside that window
         * would have agreed with both rules and proved nothing.
         */
        t->ar6 = 32L;
        t->ar5 = -32L;
        t->ar4 = -1L;
        say("a gradient that only overruns after the edge opens", fire(),
            OSMGA_HW3D_E_TEXCOORD);
    }

    printf("\n6. a left edge that opens LEFTWARD -- where is the origin?\n");
    blank();
    {
        OSMGAHW3DTri *t = setup(1024UL, 40UL, 8UL, 32UL,
                                (long)(OSMGA_HW3D_TEX_SPAN / DIM), 0UL);
        unsigned long uAt9, uAt40, uAt47;

        /* the left edge takes one column per row, to the LEFT */
        t->ar0 = 32L;
        t->ar2 = -32L;
        t->ar1 = -1L;
        t->sgn = 0x2L;                  /* left edge decreasing */
        /* start at texel 32, so a coordinate that runs backwards from the
         * origin is visible instead of being hidden at zero */
        batch->state.tmr[6] = (long)(32UL * (OSMGA_HW3D_TEX_SPAN / DIM));
        say("a left edge opening leftward", fire(), OSMGA_HW3D_OK);

        uAt9  = colour[31UL * STRIDE_DW +  9UL];
        uAt40 = colour[31UL * STRIDE_DW + 40UL];
        uAt47 = colour[31UL * STRIDE_DW + 47UL];
        printf("         bottom row (v,u): x=9 -> (%lu,%lu), x=40 -> (%lu,%lu),"
               " x=47 -> (%lu,%lu)\n",
               uAt9 >> 8, uAt9 & 0xFFUL, uAt40 >> 8, uAt40 & 0xFFUL,
               uAt47 >> 8, uAt47 & 0xFFUL);
        printf("         u of 1, 32, 39 means the origin is row 0's left;"
               " 41, 63, 63 would mean the screen\n");
    }

    printf("\n6b. the negative side of the anchor -- the check now sees it\n");
    {
        static const long starts[3] = { 0L, 31L, 30L };
        static const int wantOK[3]  = { 0, 1, 0 };
        static const char *label[3] = {
            "a left-opening edge with a zero start",
            "the same edge with a start that covers it",
            "one texel short of covering it"
        };
        int k;

        for (k = 0; k < 3; k++) {
            OSMGAHW3DTri *t;
            unsigned ver;

            blank();
            t = setup(1024UL, 40UL, 8UL, 32UL,
                      (long)(OSMGA_HW3D_TEX_SPAN / DIM), 0UL);
            t->ar0 = 32L;
            t->ar2 = -32L;
            t->ar1 = -1L;
            t->sgn = 0x2L;                  /* left edge decreasing */
            batch->state.tmr[6] = starts[k] * (long)(OSMGA_HW3D_TEX_SPAN / DIM);
            ver = fire();
            say(label[k], ver, wantOK[k] ? OSMGA_HW3D_OK
                                         : OSMGA_HW3D_E_TEXCOORD);
            if (wantOK[k] && ver == OSMGA_HW3D_OK) {
                unsigned long lo  = colour[31UL * STRIDE_DW +  9UL];
                unsigned long mid = colour[31UL * STRIDE_DW + 40UL];
                unsigned long hi  = colour[31UL * STRIDE_DW + 47UL];
                unsigned long texDirty = 0UL;

                printf("         bottom row (v,u): x=9 -> (%lu,%lu),"
                       " x=40 -> (%lu,%lu), x=47 -> (%lu,%lu)\n",
                       lo >> 8, lo & 0xFFUL, mid >> 8, mid & 0xFFUL,
                       hi >> 8, hi & 0xFFUL);
                if ((lo & 0xFFUL) == 0UL && (mid & 0xFFUL) == 31UL &&
                    (hi & 0xFFUL) == 38UL)
                    printf("   ok    %-52s\n",
                           "and it draws 0, 31, 38 as the anchor says");
                else {
                    printf("   FAIL  %-52s\n",
                           "and it draws 0, 31, 38 as the anchor says");
                    failures++;
                }
                for (r = 0UL; r < DIM; r++)
                    for (c = 0UL; c < DIM; c++)
                        if (tex[r * DIM + c] != ((r << 8) | c)) texDirty++;
                if (texDirty == 0UL)
                    printf("   ok    %-52s\n", "the texture is untouched");
                else {
                    printf("   FAIL  %-52s %lu\n", "the texture is untouched",
                           texDirty);
                    failures++;
                }
            }
        }
    }

    printf("\n6c. and the same question for v: two rows apart, one batch each\n");
    {
        int k;

        for (k = 0; k < 2; k++) {
            OSMGAHW3DTri *t;
            unsigned long y0 = (k == 0) ? 0UL : 17UL;   /* 17, not a texture
                                                         * period -- 64 would
                                                         * let a wrap look
                                                         * like a restart */
            unsigned long got;

            blank();
            t = setup(1024UL, 0UL, 8UL, 8UL,
                      (long)(OSMGA_HW3D_TEX_SPAN / DIM), 0UL);
            t->y = (long)y0;
            batch->state.tmr[7] = (long)(32UL * (OSMGA_HW3D_TEX_SPAN / DIM));
            (void)fire();
            got = colour[y0 * STRIDE_DW + 0UL];
            printf("         first row at y=%2lu -> (v,u) = (%lu,%lu)\n",
                   y0, got >> 8, got & 0xFFUL);
        }
        printf("         v of 32 both times means the origin is the primitive;"
               " 32 then 49 would mean the screen\n");
    }

    printf("\n7. bilinear, and a negative gradient\n");
    blank();
    (void)setup(1024UL, 0UL, 32UL, 32UL, (long)step, OSMGA_HW3D_TEXF_BILIN);
    say("bilinear filtering on the same triangle", fire(), OSMGA_HW3D_OK);
    blank();
    {
        (void)setup(1024UL, 0UL, 32UL, 32UL, -(long)step, 0UL);
        batch->state.tmr[6] = (long)(step * 31UL);   /* start high so every
                                                      * drawn pixel stays
                                                      * non-negative */
        say("a negative u gradient with a start that covers it", fire(),
            OSMGA_HW3D_OK);
    }

    printf("\n8. two textured primitives in ONE batch -- does the coordinate\n");
    printf("   restart at the second, or carry on from the first?\n");
    {
        /*
         * Every origin measurement so far submitted ONE triangle per batch,
         * and the texture state is written once before the triangle loop --
         * so "restarts at every primitive" was really "restarts at every
         * submission".  The batch-maximum reasoning in the validator needs
         * the stronger fact.  Nothing periodic is used: 11 columns, then a
         * second primitive at column 17 and row 20.
         */
        OSMGAHW3DTri *t;
        unsigned long a, b2;
        long texel = (long)(OSMGA_HW3D_TEX_SPAN / DIM);

        blank();
        t = setup(1024UL, 0UL, 11UL, 8UL, texel, 0UL);
        batch->state.tmr[6] = 5L * texel;
        batch->state.tmr[7] = 3L * texel;
        batch->triCount = 2UL;
        batch->tri[1] = batch->tri[0];
        batch->tri[1].y = 20L;
        batch->tri[1].fxbndry = (28UL << 16) | 17UL;
        say("a two-primitive textured batch", fire(), OSMGA_HW3D_OK);

        a  = colour[0UL * STRIDE_DW + 0UL];
        b2 = colour[20UL * STRIDE_DW + 17UL];
        printf("         first primitive  at (0,0)   -> (v,u) = (%lu,%lu)\n",
               a >> 8, a & 0xFFUL);
        printf("         second primitive at (17,20) -> (v,u) = (%lu,%lu)\n",
               b2 >> 8, b2 & 0xFFUL);
        printf("         (3,5) twice means it restarts inside the batch;"
               " (23,22) would mean it carries on\n");
        printf("         u restarts (%lu = the start); v does not (%lu"
               " = start + the first primitive's %d rows)\n",
               b2 & 0xFFUL, b2 >> 8, 8);
    }

    printf("\n8b. three primitives -- is v exactly the running sum of rows?\n");
    {
        OSMGAHW3DTri *t;
        long texel = (long)(OSMGA_HW3D_TEX_SPAN / DIM);
        unsigned long p0, p1, p2;

        blank();
        t = setup(1024UL, 0UL, 11UL, 8UL, texel, 0UL);
        batch->state.tmr[6] = 5L * texel;
        batch->state.tmr[7] = 3L * texel;
        batch->triCount = 3UL;
        batch->tri[1] = batch->tri[0];
        batch->tri[1].y = 20L;
        batch->tri[1].fxbndry = (28UL << 16) | 17UL;
        batch->tri[2] = batch->tri[0];
        batch->tri[2].y = 40L;
        batch->tri[2].h = 8L; batch->tri[2].ar0 = 8L; batch->tri[2].ar6 = 8L;
        batch->tri[2].fxbndry = (40UL << 16) | 29UL;
        say("three textured primitives", fire(), OSMGA_HW3D_OK);
        p0 = colour[ 0UL * STRIDE_DW +  0UL];
        p1 = colour[20UL * STRIDE_DW + 17UL];
        p2 = colour[40UL * STRIDE_DW + 29UL];
        printf("         v at each primitive's first row: %lu, %lu, %lu"
               "  (a running sum would be 3, 11, 19)\n",
               p0 >> 8, p1 >> 8, p2 >> 8);
        printf("         u at each: %lu, %lu, %lu  (a per-primitive anchor"
               " gives 5, 5, 5)\n",
               p0 & 0xFFUL, p1 & 0xFFUL, p2 & 0xFFUL);
    }

    printf("\n8c. does an UNTEXTURED primitive in the batch move v too?\n");
    {
        OSMGAHW3DTri *t;
        long texel = (long)(OSMGA_HW3D_TEX_SPAN / DIM);
        unsigned long p1;

        blank();
        t = setup(1024UL, 0UL, 11UL, 8UL, texel, 0UL);
        batch->state.tmr[6] = 5L * texel;
        batch->state.tmr[7] = 3L * texel;
        batch->triCount = 2UL;
        batch->tri[1] = batch->tri[0];
        batch->tri[1].y = 20L;
        batch->tri[1].fxbndry = (28UL << 16) | 17UL;
        /* the FIRST one is a plain trapezoid; only the second is textured */
        batch->tri[0].dwgctl = 0x0004UL | 0x0070UL;
        say("one flat primitive then one textured", fire(), OSMGA_HW3D_OK);
        p1 = colour[20UL * STRIDE_DW + 17UL];
        printf("         the textured one starts at v = %lu"
               "  (3 means the flat one did not move it, 11 means it did)\n",
               p1 >> 8);
    }

    printf("\n8d. heights that DIFFER -- a sum, or a constant per primitive?\n");
    {
        /*
         * 8b used three primitives of eight rows each, so v = 3, 11, 19 fits
         * "the sum of their heights" and "eight per primitive" equally well.
         * Three samples of one height cannot tell those apart.  Different
         * heights can: 5, 11, a flat 7, then 3.
         */
        OSMGAHW3DTri *t;
        long texel = (long)(OSMGA_HW3D_TEX_SPAN / DIM);
        unsigned long a, b2, d;

        blank();
        t = setup(1024UL, 0UL, 11UL, 5UL, texel, 0UL);
        batch->state.tmr[6] = 5L * texel;
        batch->state.tmr[7] = 3L * texel;
        batch->triCount = 4UL;
        batch->tri[1] = batch->tri[0];
        batch->tri[1].y = 8L;  batch->tri[1].h = 11L;
        batch->tri[1].ar0 = 11L; batch->tri[1].ar6 = 11L;
        batch->tri[1].fxbndry = (28UL << 16) | 17UL;
        batch->tri[2] = batch->tri[0];          /* FLAT, seven rows */
        batch->tri[2].dwgctl = 0x0004UL | 0x0070UL;
        batch->tri[2].y = 24L; batch->tri[2].h = 7L;
        batch->tri[2].ar0 = 7L; batch->tri[2].ar6 = 7L;
        batch->tri[2].fxbndry = (40UL << 16) | 29UL;
        batch->tri[3] = batch->tri[0];
        batch->tri[3].y = 34L; batch->tri[3].h = 3L;
        batch->tri[3].ar0 = 3L; batch->tri[3].ar6 = 3L;
        batch->tri[3].fxbndry = (52UL << 16) | 41UL;
        say("four primitives, heights 5, 11, flat 7, 3", fire(), OSMGA_HW3D_OK);

        a  = colour[ 0UL * STRIDE_DW +  0UL];
        b2 = colour[ 8UL * STRIDE_DW + 17UL];
        d  = colour[34UL * STRIDE_DW + 41UL];
        printf("         v at the three textured firsts: %lu, %lu, %lu\n",
               a >> 8, b2 >> 8, d >> 8);
        printf("         a sum of heights gives 3, 8, 19;"
               " a constant eight would give 3, 11, 19+\n");
        if ((a >> 8) == 3UL && (b2 >> 8) == 8UL && (d >> 8) == 19UL)
            printf("   ok    %-52s\n", "v is the sum of the textured heights");
        else {
            printf("   FAIL  %-52s\n", "v is the sum of the textured heights");
            failures++;
        }
    }

    printf("\n8e. does an EMPTY textured primitive step the accumulator?\n");
    {
        OSMGAHW3DTri *t;
        long texel = (long)(OSMGA_HW3D_TEX_SPAN / DIM);
        unsigned long a, c2;

        blank();
        /* a narrow destination, because an empty textured primitive makes the
         * check fall back to the clip and a 1024-wide clip would refuse the
         * batch for a reason that has nothing to do with the question */
        t = setup(64UL, 0UL, 11UL, 5UL, texel, 0UL);
        batch->state.tmr[6] = 5L * texel;
        batch->state.tmr[7] = 3L * texel;
        batch->triCount = 3UL;
        batch->tri[1] = batch->tri[0];          /* textured but EMPTY */
        batch->tri[1].y = 8L;  batch->tri[1].h = 6L;
        batch->tri[1].ar0 = 6L; batch->tri[1].ar6 = 6L;
        batch->tri[1].fxbndry = (20UL << 16) | 20UL;
        batch->tri[2] = batch->tri[0];
        batch->tri[2].y = 16L; batch->tri[2].h = 4L;
        batch->tri[2].ar0 = 4L; batch->tri[2].ar6 = 4L;
        batch->tri[2].fxbndry = (35UL << 16) | 24UL;
        /*
         * This is how the accumulator's behaviour for an empty primitive was
         * measured, and what it said: v at the first and third primitives was
         * 3 and 14, so the six empty rows stepped it.  The batch is refused
         * now -- a textured primitive that draws nothing makes a fetch nobody
         * can observe -- so the reading is kept here as the record and the
         * case asserts the refusal.
         */
        say("a batch containing one", fire(), OSMGA_HW3D_E_TRIEMPTY);
        (void)a; (void)c2;
    }

    printf("\n8f. u has a y component too (TMR2) -- does IT accumulate?\n");
    {
        /*
         * The validator hands the batch total to the u check as well as the
         * v check, which is conservative if u's y index re-seeds per
         * primitive and exact if it accumulates.  Which it is has not been
         * measured: every u measurement so far had TMR2 = 0.
         */
        OSMGAHW3DTri *t;
        long texel = (long)(OSMGA_HW3D_TEX_SPAN / DIM);
        unsigned long a, b3;

        blank();
        t = setup(64UL, 0UL, 11UL, 8UL, 0L, 0UL);   /* no x gradient */
        batch->state.tmr[6] = 0L;
        batch->state.tmr[7] = 0L;
        batch->state.tmr[2] = texel;                /* one texel per ROW, in u */
        batch->state.tmr[3] = 0L;
        batch->triCount = 2UL;
        batch->tri[1] = batch->tri[0];
        batch->tri[1].y = 16L;
        batch->tri[1].fxbndry = (28UL << 16) | 17UL;
        say("two primitives with a u-per-row gradient", fire(), OSMGA_HW3D_OK);
        a  = colour[ 0UL * STRIDE_DW +  0UL];
        b3 = colour[16UL * STRIDE_DW + 17UL];
        printf("         u at the two firsts: %lu, %lu\n",
               a & 0xFFUL, b3 & 0xFFUL);
        printf("         8 means u's row index accumulates like v;"
               " 0 means it re-seeds with u\n");
    }

    printf("\n9. the vertical span is the batch total, not the tallest\n");
    {
        /*
         * v runs on across the textured primitives, so N of them reach N
         * times as far.  With a y gradient of a sixty-fourth of the budget
         * per row and eight rows each, eight primitives spend 8257536 of
         * 8388608 and nine spend 9306112: the boundary is between them, and
         * a check that looked at the tallest alone would accept all of them.
         */
        static const unsigned long counts[3] = { 1UL, 8UL, 9UL };
        static const int wantOK[3] = { 1, 1, 0 };
        int k;

        for (k = 0; k < 3; k++) {
            unsigned long n;
            char name[64];

            blank();
            (void)setup(1024UL, 0UL, 11UL, 8UL,
                        (long)(OSMGA_HW3D_TEX_SPAN / DIM), 0UL);
            batch->state.tmr[3] = (long)(OSMGA_HW3D_TEX_COORD_MAX / 64UL);
            batch->triCount = counts[k];
            for (n = 1UL; n < counts[k]; n++)
                batch->tri[n] = batch->tri[0];
            sprintf(name, "%lu textured primitives of eight rows", counts[k]);
            say(name, fire(), wantOK[k] ? OSMGA_HW3D_OK
                                        : OSMGA_HW3D_E_TEXCOORD);
        }
    }

    printf("\n9b. an empty primitive must not hide the accumulated height\n");
    {
        /*
         * The empty-primitive fallback used to revert BOTH axes to the clip,
         * so one empty primitive discarded the total of every other textured
         * primitive in the batch.  Seven drawn plus one empty totals 64 rows
         * and fits; eight plus one totals 72 and does not.  Under the old
         * fallback all of them were sixty-three rows and all were accepted.
         */
        static const unsigned long drawn[2] = { 7UL, 8UL };
        static const int wantOK[2] = { 1, 0 };
        int k;

        for (k = 0; k < 2; k++) {
            unsigned long n;
            char name[72];

            blank();
            (void)setup(64UL, 0UL, 11UL, 8UL, 0L, 0UL);   /* no x gradient, so
                                                           * the x fallback
                                                           * cannot be the
                                                           * reason */
            batch->state.tmr[3] = (long)(OSMGA_HW3D_TEX_COORD_MAX / 64UL);
            batch->triCount = drawn[k] + 1UL;
            for (n = 1UL; n < drawn[k]; n++)
                batch->tri[n] = batch->tri[0];
            batch->tri[drawn[k]] = batch->tri[0];
            batch->tri[drawn[k]].fxbndry = (20UL << 16) | 20UL;   /* empty */
            sprintf(name, "%lu drawn plus one empty, %lu rows in all",
                    drawn[k], (drawn[k] + 1UL) * 8UL);
            (void)wantOK[k];
            /* Both are refused now, and for the empty primitive rather than
             * for the total -- which is the point: the total can no longer be
             * hidden behind one, because one is not allowed. */
            say(name, fire(), OSMGA_HW3D_E_TRIEMPTY);
        }
    }

    printf("\n9c. does a textured STATE TRANSITION reset the accumulator?\n");
    {
        /*
         * The encoder writes the texture registers once before the triangle
         * loop but rewrites DWGCTL for every primitive.  If moving between
         * atype I and atype ZI re-seeded the vertical accumulator, the check
         * would be too wide rather than too narrow -- safe, but worth
         * knowing.  Depth is given an origin inside the window so the ZI
         * primitive is legal.
         */
        OSMGAHW3DTri *t;
        long texel = (long)(OSMGA_HW3D_TEX_SPAN / DIM);
        unsigned long a, c2;

        blank();
        t = setup(64UL, 0UL, 11UL, 3UL, texel, 0UL);
        batch->state.tmr[6] = 5L * texel;
        batch->state.tmr[7] = 3L * texel;
        batch->state.zorg = 5UL * 1024UL * 1024UL;
        batch->triCount = 3UL;
        batch->tri[1] = batch->tri[0];
        batch->tri[1].dwgctl = OSMGA_HW3D_OPCODE_TEX |
                               (OSMGA_HW3D_ATYPE_ZI << 4);
        batch->tri[1].y = 8L;  batch->tri[1].h = 13L;
        batch->tri[1].ar0 = 13L; batch->tri[1].ar6 = 13L;
        batch->tri[1].fxbndry = (28UL << 16) | 17UL;
        batch->tri[2] = batch->tri[0];
        batch->tri[2].y = 24L; batch->tri[2].h = 5L;
        batch->tri[2].ar0 = 5L; batch->tri[2].ar6 = 5L;
        batch->tri[2].fxbndry = (52UL << 16) | 41UL;
        {
            unsigned ver = fire();

            printf("         verdict %u%s\n", ver,
                   ver == OSMGA_HW3D_OK ? "" : " (not accepted -- see below)");
            if (ver == OSMGA_HW3D_OK) {
                a  = colour[ 0UL * STRIDE_DW +  0UL];
                c2 = colour[24UL * STRIDE_DW + 41UL];
                printf("         v at the first and the third: %lu, %lu\n",
                       a >> 8, c2 >> 8);
                printf("         19 means the ZI primitive stepped it too"
                       " (3+3+13); 3 means the transition reset it\n");
                if ((a >> 8) == 3UL && (c2 >> 8) == 19UL)
                    printf("   ok    %-52s\n",
                           "an atype transition does not reset it");
                else {
                    printf("   FAIL  %-52s\n",
                           "an atype transition does not reset it");
                    failures++;
                }
            } else {
                printf("   FAIL  %-52s verdict %u\n",
                       "the state-transition batch is accepted", ver);
                failures++;
            }
        }
    }

    printf("\n9d. does the accumulator carry ACROSS submissions?\n");
    {
        /*
         * If it did, the per-batch total this check uses would be right only
         * for the first batch of a sequence.  The encoder writes TMR6 and
         * TMR7 inside the block it emits ONCE per batch, before the triangle
         * loop, so it should be re-seeded every time -- read, then measured,
         * because reading has been wrong before.
         */
        int pass;
        unsigned long first[2];

        for (pass = 0; pass < 2; pass++) {
            OSMGAHW3DTri *t;
            long texel = (long)(OSMGA_HW3D_TEX_SPAN / DIM);
            unsigned long n;

            blank();
            t = setup(64UL, 0UL, 11UL, 8UL, texel, 0UL);
            batch->state.tmr[6] = 5L * texel;
            batch->state.tmr[7] = 3L * texel;
            batch->triCount = 3UL;
            for (n = 1UL; n < 3UL; n++) {
                batch->tri[n] = batch->tri[0];
                batch->tri[n].y = (long)(n * 8UL);
                batch->tri[n].fxbndry = ((n * 16UL + 11UL) << 16) | (n * 16UL);
            }
            (void)fire();
            first[pass] = colour[0UL * STRIDE_DW + 0UL] >> 8;
        }
        printf("         v at the first primitive, two submissions: %lu, %lu\n",
               first[0], first[1]);
        printf("         3 both times means each batch re-seeds it;"
               " 27 the second time would mean it carries\n");
        if (first[0] == 3UL && first[1] == 3UL)
            printf("   ok    %-52s\n", "every batch starts the accumulator afresh");
        else {
            printf("   FAIL  %-52s\n", "every batch starts the accumulator afresh");
            failures++;
        }
    }

    printf("\n10. a direction bit the walk does not model\n");
    {
        OSMGAHW3DTri *t;

        blank();
        t = setup(1024UL, 0UL, 11UL, 8UL,
                  (long)(OSMGA_HW3D_TEX_SPAN / DIM), 0UL);
        t->sgn = 0x4L;
        say("sgn bit 0x4", fire(), OSMGA_HW3D_E_TRISGN);
        blank();
        t = setup(1024UL, 0UL, 11UL, 8UL,
                  (long)(OSMGA_HW3D_TEX_SPAN / DIM), 0UL);
        t->sgn = 0x22L;
        say("sgn bits 0x22, which it does model", fire(), OSMGA_HW3D_OK);
    }

    printf("\n11. where in the pixel is the coordinate sampled?\n");
    {
        /*
         * Everything so far used one texel per pixel, where the left edge and
         * the centre of a pixel give the same texel and the question does not
         * arise.  Two texels per pixel separates them: with a start of zero,
         * pixel zero reads texel 0 if the sample is at the pixel's left edge
         * and texel 1 if it is at the centre.  The Mesa side cannot place a
         * texture without this.
         */
        OSMGAHW3DTri *t;
        long texel = (long)(OSMGA_HW3D_TEX_SPAN / DIM);
        unsigned long a, b4, c3;

        blank();
        t = setup(64UL, 0UL, 16UL, 8UL, 2L * texel, 0UL);
        batch->state.tmr[3] = 0L;
        batch->state.tmr[6] = 0L;
        batch->state.tmr[7] = 0L;
        say("two texels per pixel from a zero start", fire(), OSMGA_HW3D_OK);
        a  = colour[0UL * STRIDE_DW + 0UL] & 0xFFUL;
        b4 = colour[0UL * STRIDE_DW + 1UL] & 0xFFUL;
        c3 = colour[0UL * STRIDE_DW + 2UL] & 0xFFUL;
        printf("         u at columns 0,1,2: %lu, %lu, %lu\n", a, b4, c3);
        printf("         0,2,4 means the sample is at the pixel's left edge;"
               " 1,3,5 means its centre\n");

        /* and a half-texel start, which separates them again the other way */
        blank();
        t = setup(64UL, 0UL, 16UL, 8UL, texel, 0UL);
        batch->state.tmr[3] = 0L;
        batch->state.tmr[6] = texel / 2L;
        batch->state.tmr[7] = 0L;
        say("one texel per pixel from a half-texel start", fire(),
            OSMGA_HW3D_OK);
        a  = colour[0UL * STRIDE_DW + 0UL] & 0xFFUL;
        b4 = colour[0UL * STRIDE_DW + 1UL] & 0xFFUL;
        printf("         u at columns 0,1: %lu, %lu\n", a, b4);
        printf("         a start of half a texel lands mid-texel either way,"
               " so this shows the rounding, not the position\n");
    }

    printf("\n12. inside ONE primitive: how do the row terms apply?\n");
    {
        /*
         * Every earlier measurement of TMR2/TMR3 compared the FIRST rows of
         * two primitives.  Within a primitive, across rows, nothing has been
         * measured -- and the model built from those measurements does not
         * reproduce a real drawn triangle.
         *
         * A rectangle, so the left edge does not move: u gets one texel per
         * ROW and nothing per column, v gets one per COLUMN and nothing per
         * row.  Then u reads the row and v reads the column, and each says
         * what its own cross term does.
         */
        OSMGAHW3DTri *t;
        long texel = (long)(OSMGA_HW3D_TEX_SPAN / DIM);
        unsigned long k;

        blank();
        t = setup(64UL, 0UL, 16UL, 8UL, 0L, 0UL);   /* tmr[0] = 0 */
        /*
         * DIFFERENT values, three texels against five.  This case gave both
         * the same value once, so either assignment of the two cross terms
         * produced the same picture and it read as a confirmation of the
         * wrong one.  With three and five the picture says which is which.
         */
        batch->state.tmr[1] = 3L * texel;
        batch->state.tmr[2] = 5L * texel;
        batch->state.tmr[3] = 0L;
        batch->state.tmr[6] = 0L;
        batch->state.tmr[7] = 0L;
        say("u per row, v per column, on a rectangle", fire(), OSMGA_HW3D_OK);
        printf("         TMR1 = 3 texels, TMR2 = 5 texels\n");
        printf("         column 0, rows 0..5  u =");
        for (k = 0UL; k < 6UL; k++)
            printf(" %lu", colour[k * STRIDE_DW + 0UL] & 0xFFUL);
        printf("   (0 3 6 9 12 15 means TMR1 is u per ROW)\n");
        printf("         row 0, columns 0..5  v =");
        for (k = 0UL; k < 6UL; k++)
            printf(" %lu", colour[0UL * STRIDE_DW + k] >> 8);
        printf("   (0 5 10 15 20 25 means TMR2 is v per COLUMN)\n");
    }

    printf("\n13. a SLOPED left edge: is x measured from the row or the anchor?\n");
    {
        /*
         * The left edge moves one column right per row.  u gets one texel per
         * column and nothing per row.  If x is measured from the primitive's
         * anchor, the first pixel of row r reads texel r; if it restarts at
         * each row's own left edge, every row's first pixel reads 0.
         */
        OSMGAHW3DTri *t;
        long texel = (long)(OSMGA_HW3D_TEX_SPAN / DIM);
        unsigned long k;

        blank();
        t = setup(64UL, 0UL, 8UL, 8UL, texel, 0UL);
        t->ar0 = 8L; t->ar2 = -8L; t->ar1 = -1L;    /* left edge steps right */
        t->sgn = 0L;
        t->ar6 = 8L; t->ar5 = -8L; t->ar4 = -1L;    /* right edge too */
        batch->state.tmr[1] = 0L;
        batch->state.tmr[2] = 0L;
        batch->state.tmr[3] = 0L;
        batch->state.tmr[6] = 0L;
        batch->state.tmr[7] = 0L;
        say("a left edge stepping right, u per column", fire(), OSMGA_HW3D_OK);
        printf("         each row's FIRST pixel, rows 0..5  u =");
        for (k = 0UL; k < 6UL; k++)
            printf(" %lu", colour[k * STRIDE_DW + k] & 0xFFUL);
        printf("\n         0 1 2 3 4 5 means x is from the anchor;"
               " 0 0 0 0 0 0 means it restarts each row\n");
    }

    printf("\n14. does the hardware honour a NEGATIVE increment?\n");
    {
        /*
         * The validator was changed to allow negative increments, on the
         * grounds that a triangle whose texture runs the other way across the
         * screen is in no way exotic.  That was reasoning, not measurement:
         * the probe that followed only read the verdict.  A real textured
         * triangle then came out wrong, and isolating its terms showed v not
         * moving at all across a row where its increment was negative.
         *
         * Each of the four increments is given a negative value with a start
         * high enough to keep the coordinate non-negative.  A coordinate that
         * falls across the span is honoured; one that stands still is not.
         */
        static const int idx[4]  = { 0, 1, 2, 3 };
        static const char *nm[4] = { "u per column (TMR0)", "v per column (TMR1)",
                                     "u per row (TMR2)",    "v per row (TMR3)" };
        long texel = (long)(OSMGA_HW3D_TEX_SPAN / DIM);
        int j;

        for (j = 0; j < 4; j++) {
            OSMGAHW3DTri *t;
            unsigned long a, b5;
            unsigned ver;
            int isU = (idx[j] == 0 || idx[j] == 2);
            int isCol = (idx[j] == 0 || idx[j] == 1);

            blank();
            t = setup(64UL, 0UL, 16UL, 8UL, 0L, 0UL);
            batch->state.tmr[0] = 0L; batch->state.tmr[1] = 0L;
            batch->state.tmr[2] = 0L; batch->state.tmr[3] = 0L;
            batch->state.tmr[idx[j]] = -texel;      /* one texel DOWN */
            /* enough to cover sixteen columns or eight rows going down */
            batch->state.tmr[6] = 40L * texel;
            batch->state.tmr[7] = 40L * texel;
            ver = fire();
            if (isCol) {
                a  = colour[0UL * STRIDE_DW + 0UL];
                b5 = colour[0UL * STRIDE_DW + 5UL];
            } else {
                a  = colour[0UL * STRIDE_DW + 0UL];
                b5 = colour[5UL * STRIDE_DW + 0UL];
            }
            printf("   %-22s verdict %u  start 40, five steps later:"
                   " %lu -> %lu   %s\n",
                   nm[j], ver,
                   isU ? (a & 0xFFUL) : (a >> 8),
                   isU ? (b5 & 0xFFUL) : (b5 >> 8),
                   (ver != OSMGA_HW3D_OK) ? "(refused)"
                   : ((isU ? (b5 & 0xFFUL) : (b5 >> 8)) == 35UL
                       ? "honoured" : "NOT honoured"));
        }
    }

    printf("\n15. constant, or a sample taken slightly inside the pixel?\n");
    {
        /*
         * The hardware's coordinate runs about five hundred units ahead of
         * the model.  Two explanations fit the data so far: a constant added
         * to the coordinate, or a sample taken a fraction of a pixel inside,
         * which would scale with the increment.  A small increment separates
         * them: with 500 per column the first texel boundary lands at column
         * 32 if a constant is added and at 33 if the offset is a fraction of
         * a pixel.
         */
        static const long incs[3] = { 500L, 1000L, 5533L };
        int j;

        for (j = 0; j < 3; j++) {
            OSMGAHW3DTri *t;
            unsigned long k, first = 0UL;

            blank();
            t = setup(64UL, 0UL, 40UL, 4UL, incs[j], 0UL);
            batch->state.tmr[1] = 0L; batch->state.tmr[2] = 0L;
            batch->state.tmr[3] = 0L;
            batch->state.tmr[6] = 0L; batch->state.tmr[7] = 0L;
            (void)fire();
            for (k = 0UL; k < 40UL; k++)
                if ((colour[0UL * STRIDE_DW + k] & 0xFFUL) != 0UL) {
                    first = k; break;
                }
            printf("   increment %5ld: first column with texel 1 is %lu\n",
                   incs[j], first);
        }
        printf("   a constant predicts 32, 16, 3;"
               " a fraction of a pixel predicts 33, 17, 3\n");
    }

    printf("\n16. the constant, exactly\n");
    {
        /*
         * With every increment at zero the coordinate is the start and
         * nothing else, so the start at which the texel turns over gives the
         * constant directly: it flips when start + K reaches one texel.
         */
        long lo = 15000L, hi = 16400L, mid;
        long flipU = -1L, flipV = -1L;
        int it;

        for (it = 0; it < 16 && lo < hi; it++) {
            OSMGAHW3DTri *t;
            unsigned long p;

            mid = (lo + hi) / 2L;
            blank();
            t = setup(64UL, 0UL, 8UL, 4UL, 0L, 0UL);
            batch->state.tmr[1] = 0L; batch->state.tmr[2] = 0L;
            batch->state.tmr[3] = 0L;
            batch->state.tmr[6] = mid;
            batch->state.tmr[7] = mid;
            (void)fire();
            p = colour[0UL * STRIDE_DW + 0UL];
            if ((p & 0xFFUL) != 0UL) hi = mid; else lo = mid + 1L;
        }
        flipU = lo;
        lo = 15000L; hi = 16400L;
        for (it = 0; it < 16 && lo < hi; it++) {
            OSMGAHW3DTri *t;
            unsigned long p;

            mid = (lo + hi) / 2L;
            blank();
            t = setup(64UL, 0UL, 8UL, 4UL, 0L, 0UL);
            batch->state.tmr[1] = 0L; batch->state.tmr[2] = 0L;
            batch->state.tmr[3] = 0L;
            batch->state.tmr[6] = mid;
            batch->state.tmr[7] = mid;
            (void)fire();
            p = colour[0UL * STRIDE_DW + 0UL];
            if ((p >> 8) != 0UL) hi = mid; else lo = mid + 1L;
        }
        flipV = lo;
        printf("   u turns over at start %ld  ->  K = %ld\n",
               flipU, (long)OSMGA_HW3D_TEX_SPAN / 64L - flipU);
        printf("   v turns over at start %ld  ->  K = %ld\n",
               flipV, (long)OSMGA_HW3D_TEX_SPAN / 64L - flipV);
    }

    printf("\n17. does the constant depend on the gradient?\n");
    {
        /*
         * The constant was measured with every increment at zero, and again
         * at 500, 1000 and 5533 where it held.  Then a correction built on it
         * broke two cases whose increments were 16384 and 32768, while the
         * scene it was built for -- 8192 per pixel -- improved.  So the
         * question is whether the constant depends on the gradient, and where
         * it changes.
         *
         * The turnover start is measured with one increment held at each
         * magnitude.  The running kernel subtracts 511 before writing the
         * register, so a turnover at 16384 means the engine adds 511 back and
         * one at 16895 means it adds nothing.
         */
        static const long mags[9] = { 0L, 500L, 1000L, 5533L, 8192L,
                                      12288L, 16384L, 24576L, 32768L };
        int j;

        printf("   %8s %10s %8s\n", "increment", "turnover", "implies K");
        for (j = 0; j < 9; j++) {
            long lo = 15000L, hi = 17500L, mid;
            int it;

            for (it = 0; it < 16 && lo < hi; it++) {
                OSMGAHW3DTri *t;

                mid = (lo + hi) / 2L;
                blank();
                t = setup(64UL, 0UL, 8UL, 4UL, mags[j], 0UL);
                batch->state.tmr[1] = 0L; batch->state.tmr[2] = 0L;
                batch->state.tmr[3] = 0L;
                batch->state.tmr[6] = mid;
                batch->state.tmr[7] = 0L;
                (void)fire();
                if ((colour[0UL * STRIDE_DW + 0UL] & 0xFFUL) != 0UL) hi = mid;
                else lo = mid + 1L;
            }
            printf("   %8ld %10ld %8ld\n", mags[j], lo,
                   (long)OSMGA_HW3D_TEX_SPAN / 64L - lo);
        }
        printf("   the constant is the same at every magnitude\n");
    }

    printf("\n18. does the accumulation arrive short?\n");
    {
        /*
         * The correction that was tried and taken out failed further along a
         * span, not at its start.  A negative start cannot be sent from a
         * client -- the per-row check refuses the first pixel -- but the same
         * shape can be built with a POSITIVE one: a start of 15873 plus 511
         * is exactly one texel, so every column ought to land exactly on a
         * boundary and read 1 + 2c.  A column that reads 2c is the
         * accumulation arriving short.
         */
        long texel = (long)(OSMGA_HW3D_TEX_SPAN / DIM);
        unsigned long k;
        int firstShort = -1;

        {
        static const long incs[3] = { 2L, 1L, 0L };  /* texels per column:
                                                      * two, one, a half */
        int j;

        for (j = 0; j < 3; j++) {
            long inc = (incs[j] != 0L) ? incs[j] * texel : texel / 2L;
            long want;

            blank();
            (void)setup(64UL, 0UL, 40UL, 4UL, inc, 0UL);
            batch->state.tmr[1] = 0L; batch->state.tmr[2] = 0L;
            batch->state.tmr[3] = 0L;
            batch->state.tmr[6] = texel - 511L;
            batch->state.tmr[7] = 0L;
            (void)fire();
            firstShort = -1;
            printf("   increment %6ld  u:", inc);
            for (k = 0UL; k < 12UL; k++) {
                unsigned long got = colour[0UL * STRIDE_DW + k] & 0xFFUL;

                want = (texel - 511L + inc * (long)k + 511L) / texel;
                printf(" %lu", got);
                if (firstShort < 0 && got != (unsigned long)want)
                    firstShort = (int)k;
            }
            printf("   short from column %d", firstShort);
            if (firstShort >= 0)
                printf(" (coordinate %ld)",
                       texel - 511L + inc * (long)firstShort);
            printf("\n");
        }
        }
        (void)say; (void)firstShort;
    }

    printf("\n%s (%d failing)\n",
           failures ? "=== PROBLEM ===" : "=== nothing to report ===", failures);
    return failures ? 1 : 0;
}
