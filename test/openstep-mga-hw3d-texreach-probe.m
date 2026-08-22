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

/*
 * Read a drawn pixel.
 *
 * The submit call returns when the batch has been handed to the engine, not
 * when the engine has finished with it, so a read taken straight afterwards
 * can catch a column the engine has not reached yet -- and the further right
 * the column, the likelier that is.  Bisecting on such a read converges on
 * the previous iteration's picture instead of this one's.  BLANK is a value
 * no texel can produce, so waiting for it to go is a sound way to wait for
 * the column to be written.
 */
static unsigned long
pixat(unsigned long r, unsigned long c)
{
    unsigned long v, spin;

    for (spin = 0UL; spin < 2000000UL; spin++) {
        v = colour[r * STRIDE_DW + c];
        if (v != BLANK)
            return v;
    }
    return colour[r * STRIDE_DW + c];
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

    printf("\n19. is the threshold on the offset or on the coordinate?\n");
    {
        /*
         * Everything so far was measured from one start, 15873, and from
         * there "the offset reached 65536" and "the coordinate reached 65536"
         * name almost the same place -- the one column that could have told
         * them apart happened to read the same index under both.  So the
         * start has to move.
         *
         * With every gradient at zero the offset is identically zero at every
         * covered pixel, so a rule on the offset must apply the bias at every
         * start, while a rule on the coordinate must drop it once the start
         * passes 65536.  Bisecting the turnover at each texel boundary reads
         * the bias off directly: K = 511 means it was applied, K = 0 means it
         * was not.
         *
         *      boundary k    offset rule    coordinate rule    bit 16 clear
         *         1 .. 4         511             511                511
         *         5 .. 8         511               0                  0
         *         9 .. 12        511               0                511
         *
         * The last four are what separate a comparison against 65536 from a
         * test of bit 16, which the first eight cannot tell apart.
         */
        long texel = (long)(OSMGA_HW3D_TEX_SPAN / DIM);
        long k;

        long runFrom = 1L, runK = -1L;

        printf("   %14s %10s %8s\n", "boundaries", "turnover", "implies K");
        for (k = 1L; k <= 63L; k++) {
            long lo = k * texel - 1200L, hi = k * texel + 400L, mid;
            int it;

            for (it = 0; it < 16 && lo < hi; it++) {
                unsigned long p;

                mid = (lo + hi) / 2L;
                blank();
                (void)setup(64UL, 0UL, 8UL, 4UL, 0L, 0UL);
                batch->state.tmr[1] = 0L; batch->state.tmr[2] = 0L;
                batch->state.tmr[3] = 0L;
                batch->state.tmr[6] = mid;
                batch->state.tmr[7] = 0L;
                (void)fire();
                p = colour[0UL * STRIDE_DW + 0UL] & 0xFFUL;
                if ((long)p >= k) hi = mid; else lo = mid + 1L;
            }
            if (k * texel - lo != runK) {
                if (runK >= 0L)
                    printf("   %6ld .. %-6ld %10s %8ld\n",
                           runFrom, k - 1L, "", runK);
                runFrom = k;
                runK = k * texel - lo;
            }
            if (k == 63L)
                printf("   %6ld .. %-6ld %10ld %8ld\n", runFrom, k, lo, runK);
        }
        printf("   the engine's own ladder is 511 510 508 504 496;"
               " the kernel takes %ld off, so what a client sees is\n",
               (long)OSMGA_HW3D_TEX_BIAS);
        printf("   16 - g, that is 15 14 12 8 0 -- and a row of 511s would"
               " mean the correction never went out\n");
    }

    printf("\n20. the same question with a live gradient\n");
    {
        /*
         * Section 19 holds every gradient at zero, and a zero gradient could
         * in principle be a different path through the setup.  The same three
         * rules can be separated in one drawing with the gradient running:
         * from a start of five texels less 511, stepping one texel a column,
         *
         *      offset rule       5 6 7 8 8
         *      coordinate rule   4 5 6 7 8
         *      bit 16 clear      4 5 6 7 9
         *
         * all three differ, so the five pixels name the rule outright.
         */
        long texel = (long)(OSMGA_HW3D_TEX_SPAN / DIM);
        unsigned long k;
        static const unsigned long offs[5]  = { 5UL, 6UL, 7UL, 8UL, 8UL };
        static const unsigned long coord[5] = { 4UL, 5UL, 6UL, 7UL, 8UL };
        static const unsigned long bit16[5] = { 4UL, 5UL, 6UL, 7UL, 9UL };
        unsigned long got[5];
        int mo = 1, mc = 1, mb = 1;

        blank();
        (void)setup(64UL, 0UL, 8UL, 4UL, texel, 0UL);
        batch->state.tmr[1] = 0L; batch->state.tmr[2] = 0L;
        batch->state.tmr[3] = 0L;
        batch->state.tmr[6] = 5L * texel - 511L;
        batch->state.tmr[7] = 0L;
        (void)fire();
        printf("   u:");
        for (k = 0UL; k < 5UL; k++) {
            got[k] = colour[0UL * STRIDE_DW + k] & 0xFFUL;
            printf(" %lu", got[k]);
            if (got[k] != offs[k])  mo = 0;
            if (got[k] != coord[k]) mc = 0;
            if (got[k] != bit16[k]) mb = 0;
        }
        printf("   ->  %s\n",
               mo ? "the offset rule" :
               mc ? "a rule on the coordinate" :
               mb ? "bit 16 clear" : "none of the three");
    }

    printf("\n21. is the coordinate cut at the sample, or in the accumulator?\n");
    {
        /*
         * Section 19 says the coordinate loses low bits as it grows, but not
         * WHERE it loses them, and the difference decides whether the encoder
         * may compensate at all.
         *
         * If the cut happens when the sample is taken, from a coordinate that
         * accumulated exactly, then taking 511 off the start costs at most
         * g-1 units anywhere along the span.  If instead the accumulator
         * itself is cut at every step, an increment smaller than g is thrown
         * away entirely and the coordinate STALLS -- and then compensating at
         * the start would drift by a texel over a long enough span.
         *
         * Every live-gradient measurement so far used 8192, 16384 or 32768,
         * every one a multiple of every g, which is precisely the blind spot.
         * So: sit in the g = 16 region (past 2^19, texel 32 and up) and step
         * by less than 16.  The starts are chosen so that a coordinate which
         * really accumulates reaches texel 33 within the columns read.
         *
         *      increment 15 from 539760   crosses at column 28
         *      increment  5 from 540048   crosses at column 26
         *      increment  1 from 540160   crosses at column 16
         *
         * A cut accumulator never crosses at all.
         */
        static const long incs[3]   = { 15L, 5L, 1L };
        static const long begins[3] = { 539760L, 540048L, 540160L };
        static const long cross[3]  = { 28L, 26L, 16L };
        int j;

        for (j = 0; j < 3; j++) {
            unsigned long c;
            long first = -1L;

            blank();
            (void)setup(64UL, 0UL, 32UL, 4UL, incs[j], 0UL);
            batch->state.tmr[1] = 0L; batch->state.tmr[2] = 0L;
            batch->state.tmr[3] = 0L;
            /* the kernel takes OSMGA_HW3D_TEX_BIAS off on the way out,
             * and this section is about the ENGINE, so put it back */
            batch->state.tmr[6] = begins[j] + OSMGA_HW3D_TEX_BIAS;
            batch->state.tmr[7] = 0L;
            (void)fire();
            printf("   increment %2ld  u:", incs[j]);
            for (c = 0UL; c < 32UL; c++) {
                unsigned long got = pixat(0UL, c) & 0xFFUL;

                printf("%c", (got >= 33UL) ? '1' : '0');
                if (first < 0L && got >= 33UL) first = (long)c;
            }
            if (first < 0L)
                printf("   never crossed -- the accumulator is cut\n");
            else
                printf("   crossed at %ld (a cut accumulator never would;"
                       " section 22 measures the %ld)\n",
                       first, first - cross[j]);
        }
    }

    printf("\n22. how much does a step add, exactly?\n");
    {
        /*
         * Section 21 crossed a column earlier than the model in two of three
         * runs, so the effective coordinate is not exactly what the model
         * says.  Measuring it by bisecting the start turned out to be a bad
         * instrument: sixteen submissions in a row, each preceded by clearing
         * a quarter of a megabyte, and the engine does not keep up -- the
         * columns further right come back holding the blank pattern, and a
         * bisection reading those converges on nothing.
         *
         * One drawing per start, and an increment of ONE, is a better
         * instrument than any bisection: each column then advances the
         * coordinate by exactly one unit, so the column where the texel turns
         * over pins the effective coordinate to a single unit.
         *
         *      K = 33 * texel  -  start  -  (turnover column)
         *
         * A flat K across thirty-two starts is a constant bias; a K that
         * moves with the column is a per-step term.  The same sweep is then
         * repeated at increments 5 and 15 to see whether the step size
         * enters.
         */
        long texel = (long)(OSMGA_HW3D_TEX_SPAN / DIM);
        long tgt = 33L * texel;
        static const long incs[3] = { 1L, 5L, 15L };
        int j;

        for (j = 0; j < 3; j++) {
            long n;

            printf("   increment %2ld, K at each start:", incs[j]);
            for (n = 0L; n < 12L; n++) {
                long begin = tgt - 496L - incs[j] * 20L + n * incs[j];
                unsigned long c;
                long first = -1L;
                int blanks = 0;

                blank();
                (void)setup(64UL, 0UL, 32UL, 4UL, incs[j], 0UL);
                batch->state.tmr[1] = 0L; batch->state.tmr[2] = 0L;
                batch->state.tmr[3] = 0L;
                batch->state.tmr[6] = begin + OSMGA_HW3D_TEX_BIAS;
                batch->state.tmr[7] = 0L;
                (void)fire();
                for (c = 0UL; c < 32UL; c++) {
                    unsigned long got = pixat(0UL, c);

                    if (got == BLANK) { blanks++; continue; }
                    if (first < 0L && (got & 0xFFUL) >= 33UL)
                        first = (long)c;
                }
                if (blanks)
                    printf(" b%d", blanks);
                else if (first < 0L)
                    printf("   --");
                else
                    printf(" %4ld", tgt - begin - incs[j] * first);
            }
            printf("\n");
        }
        printf("   a flat row is a constant bias; a row that climbs by the"
               " increment is a per-step term\n");
        {   /* and the columns themselves, one unit of start at a time */
            long n, jj;

            for (jj = 0; jj < 3; jj++)
            for (n = 0L; n < 4L; n++) {
                long begin = tgt - 496L - incs[jj] * 20L + n;
                unsigned long c;

                blank();
                (void)setup(64UL, 0UL, 32UL, 4UL, incs[jj], 0UL);
                batch->state.tmr[1] = 0L; batch->state.tmr[2] = 0L;
                batch->state.tmr[3] = 0L;
                batch->state.tmr[6] = begin + OSMGA_HW3D_TEX_BIAS;
                batch->state.tmr[7] = 0L;
                (void)fire();
                printf("   inc %2ld start %ld  ", incs[jj], begin);
                for (c = 0UL; c < 32UL; c++)
                    printf("%c", ((pixat(0UL, c) & 0xFFUL) >= 33UL)
                                 ? '1' : '0');
                printf("\n");
            }
        }
    }

    printf("\n23. does subtracting the bias actually fix it?\n");
    {
        /*
         * The whole point of measuring the bias is to take it off in the
         * encoder, and that can be tried from here without touching the
         * kernel at all: the probe simply programs a start that already has
         * the bias removed.  If the rule is right, the texels come out as the
         * coordinate says they should.
         *
         * The case where the bias is visible is a coordinate that sits just
         * BELOW a texel boundary: adding 511 carries it over and the engine
         * reads the next texel.  A row of 600 columns stepping one unit each,
         * starting 600 below the boundary, is 600 such coordinates in one
         * drawing -- every one of them should read the texel below.
         *
         * Two bands, because the bias is not the same in both: the first
         * texel, where the granularity is one and the bias is 511, and texel
         * 40, past 2^19, where it is sixteen and 496.
         */
        long texel = (long)(OSMGA_HW3D_TEX_SPAN / DIM);
        static const long bands[2] = { 1L, 40L };
        int j;

        for (j = 0; j < 2; j++) {
            long bnd = bands[j] * texel;
            long want = bands[j] - 1L;
            int mode;

            for (mode = 0; mode < 2; mode++) {
                long begin = bnd - 600L;
                long bias = 0L, probe;
                unsigned long c, bad = 0UL, blanks = 0UL;

                if (mode)
                    bias = OSMGA_HW3D_TEX_BIAS;   /* the smallest of the ladder */
                probe = begin - bias;
                blank();
                (void)setup(1024UL, 0UL, 600UL, 4UL, 1L, 0UL);
                batch->state.tmr[1] = 0L; batch->state.tmr[2] = 0L;
                batch->state.tmr[3] = 0L;
                batch->state.tmr[6] = probe;
                batch->state.tmr[7] = 0L;
                (void)fire();
                for (c = 0UL; c < 600UL; c++) {
                    unsigned long got = pixat(0UL, c);

                    if (got == BLANK) { blanks++; continue; }
                    if ((long)(got & 0xFFUL) != want) bad++;
                }
                printf("   texel %2ld  %-13s bias %3ld  wrong %3lu of 600%s\n",
                       want, mode ? "compensated" : "as the engine is",
                       bias, bad, blanks ? "  (undrawn columns!)" : "");
            }
        }
        printf("   with the kernel correcting, the plain row is the one that"
               " matters: 15 of 600 in the first band, 1 past 2^19\n");
        printf("   the second row corrects a second time and can only"
               " UNDERSHOOT, which this shape cannot see at all\n");
    }

    printf("\n24. and the case that vetoed the first attempt\n");
    {
        /*
         * The first attempt took a fixed 511 off, and a drawing whose texture
         * lands exactly on texel boundaries came back one texel low from part
         * way along.  That is the ladder: 511 was taken off at the start and
         * only 510, then 508, was added back further out, so a coordinate
         * sitting exactly ON a boundary fell one unit below it.
         *
         * Subtracting the SMALLEST of the ladder cannot do that, and this
         * checks it where it failed before -- a start of one texel and 32768
         * a column, which is exactly on a boundary at every column, so the
         * texels must run 1, 3, 5, 7, ... with nothing lost.  Both the
         * uncorrected engine and the correction that goes in should pass it;
         * a per-band correction would not, which is why it is not what went
         * in.
         */
        long texel = (long)(OSMGA_HW3D_TEX_SPAN / DIM);
        long step32 = (long)(OSMGA_HW3D_TEX_SPAN / 32UL);
        int mode;

        for (mode = 0; mode < 3; mode++) {
            static const char *what[3] = { "the coordinate, as meant",
                                           "corrected twice, by 496",
                                           "corrected twice, by 511" };
            long bias = (mode == 0) ? 0L
                      : (mode == 1) ? OSMGA_HW3D_TEX_BIAS : 511L;
            unsigned long c, bad = 0UL;

            blank();
            (void)setup(1024UL, 0UL, 12UL, 4UL, step32, 0UL);
            batch->state.tmr[1] = 0L; batch->state.tmr[2] = 0L;
            batch->state.tmr[3] = 0L;
            batch->state.tmr[6] = texel - bias;
            batch->state.tmr[7] = 0L;
            (void)fire();
            printf("   %-28s u:", what[mode]);
            for (c = 0UL; c < 12UL; c++) {
                unsigned long got = pixat(0UL, c) & 0xFFUL;

                printf(" %lu", got);
                if (got != 1UL + 2UL * c) bad++;
            }
            printf("   %lu wrong\n", bad);
        }
        printf("   wanted 1 3 5 7 9 ...  The first row is the one that has to"
               " pass, and it is what the first attempt broke.\n");
        printf("   The other two are over-corrected by about a thousand and"
               " agree because 15 units apart is mid-texel.\n");
    }

    printf("\n25. which coordinate is the one that still misses?\n");
    {
        /*
         * Section 23 leaves one wrong of six hundred past 2^19, and the
         * ladder does not account for it: there the engine adds 496 and the
         * kernel takes 496 off, so every coordinate should read the texel it
         * names.  A count is not a diagnosis -- name the coordinate.
         *
         * A row of 33 columns stepping one unit, ending ON the boundary, is
         * every offset from 32 below it to the boundary itself.  Columns 0 to
         * 31 must read the texel below and column 32 the texel at it, so the
         * string must be 32 noughts and a one.  Three texels of the same band
         * say whether it is one place or every boundary.
         */
        long texel = (long)(OSMGA_HW3D_TEX_SPAN / DIM);
        static const long ks[3] = { 37L, 39L, 41L };
        int j;

        for (j = 0; j < 3; j++) {
            long bnd = ks[j] * texel;
            unsigned long c;

            blank();
            (void)setup(1024UL, 0UL, 33UL, 4UL, 1L, 0UL);
            batch->state.tmr[1] = 0L; batch->state.tmr[2] = 0L;
            batch->state.tmr[3] = 0L;
            batch->state.tmr[6] = bnd - 32L;
            batch->state.tmr[7] = 0L;
            (void)fire();
            printf("   texel %2ld, offsets -32..0:", ks[j]);
            for (c = 0UL; c < 33UL; c++)
                printf("%c", ((long)(pixat(0UL, c) & 0xFFUL) >= ks[j])
                             ? '1' : '0');
            printf("\n");
        }
        printf("   wanted 32 noughts and a one; a one further left is a"
               " coordinate read too high\n");
    }

    printf("\n26. what does the engine's bilinear actually do?\n");
    {
        /*
         * Section 7 asks only whether a bilinear batch is accepted.  Nobody
         * has looked at the picture, and the Mesa gate refuses GL_LINEAR
         * because of it.
         *
         * A texture whose texels name themselves cannot show a blend: between
         * texel 10 and 11 every weight rounds to 10 or 11.  So paint one that
         * can -- neighbouring texels 0 and 255 -- and the byte that comes back
         * IS the weight, to one part in 255.
         *
         * Walking one texel in 64 steps then says three things at once:
         * whether the engine blends at all, WHERE the ramp sits relative to
         * the texel boundary (GL puts it at texel centres, half a texel off
         * from the boundary), and how it rounds.
         */
        long texel = (long)(OSMGA_HW3D_TEX_SPAN / DIM);
        unsigned long r, c;
        int mode;

        for (r = 0UL; r < DIM; r++)
            for (c = 0UL; c < DIM; c++)
                tex[r * DIM + c] = ((c & 1UL) ? 0xFFUL : 0UL)
                                 | ((r & 1UL) ? 0xFF00UL : 0UL);

        for (mode = 0; mode < 2; mode++) {
            blank();
            (void)setup(1024UL, 0UL, 64UL, 4UL, texel / 64L,
                        mode ? OSMGA_HW3D_TEXF_BILIN : 0UL);
            batch->state.tmr[1] = 0L; batch->state.tmr[2] = 0L;
            batch->state.tmr[3] = 0L;
            /* start on a texel boundary, four texels in, on an EVEN texel so
             * the ramp runs from 0 towards 255 */
            batch->state.tmr[6] = 4L * texel;
            batch->state.tmr[7] = 0L;
            (void)fire();
            printf("   %-8s u:", mode ? "bilinear" : "nearest");
            for (c = 0UL; c < 64UL; c += 2UL)
                printf(" %lu", pixat(0UL, c) & 0xFFUL);
            printf("\n");
        }
        printf("   the row covers ONE texel, so nearest must be 0 throughout;"
               " the ramp says where the blend is centred\n");

        /* put the identifying texture back for anything that follows */
        for (r = 0UL; r < DIM; r++)
            for (c = 0UL; c < DIM; c++)
                tex[r * DIM + c] = (r << 8) | c;
    }

    printf("\n27. and what does it do at the edge?\n");
    {
        /*
         * With nearest sampling GL_CLAMP and GL_CLAMP_TO_EDGE are the same
         * thing, which is why the gate has been able to accept GL_CLAMP so
         * far.  Under a linear filter they part company: GL_CLAMP blends the
         * BORDER colour into the outermost half texel and CLAMP_TO_EDGE does
         * not.  Which one the engine's CLAMPUV is has never been measured,
         * and it decides which wrap mode the gate may advertise.
         *
         * Paint the two texels at each end white and the middle black, then
         * walk the outer half texel at each end.  Clamping to the edge holds
         * 255 all the way out; blending a black border falls to about half.
         */
        long texel = (long)(OSMGA_HW3D_TEX_SPAN / DIM);
        unsigned long r, c;

        for (r = 0UL; r < DIM; r++)
            for (c = 0UL; c < DIM; c++)
                tex[r * DIM + c] = (c <= 1UL || c >= DIM - 2UL) ? 0xFFUL : 0UL;

        blank();
        (void)setup(1024UL, 0UL, 32UL, 4UL, texel / 64L,
                    OSMGA_HW3D_TEXF_BILIN);
        batch->state.tmr[1] = 0L; batch->state.tmr[2] = 0L;
        batch->state.tmr[3] = 0L;
        batch->state.tmr[6] = 0L;                 /* the outer half texel */
        batch->state.tmr[7] = 0L;
        (void)fire();
        printf("   low  edge, u 0 .. half a texel: ");
        for (c = 0UL; c < 32UL; c += 4UL)
            printf(" %lu", pixat(0UL, c) & 0xFFUL);
        printf("\n");

        blank();
        (void)setup(1024UL, 0UL, 32UL, 4UL, texel / 64L,
                    OSMGA_HW3D_TEXF_BILIN);
        batch->state.tmr[1] = 0L; batch->state.tmr[2] = 0L;
        batch->state.tmr[3] = 0L;
        batch->state.tmr[6] = (long)(DIM - 1UL) * texel + texel / 2L;
        batch->state.tmr[7] = 0L;
        (void)fire();
        printf("   high edge, last half texel:     ");
        for (c = 0UL; c < 32UL; c += 4UL)
            printf(" %lu", pixat(0UL, c) & 0xFFUL);
        printf("\n");
        printf("   255 throughout is CLAMP_TO_EDGE;"
               " a fall towards 127 is GL_CLAMP with a black border\n");

        for (r = 0UL; r < DIM; r++)
            for (c = 0UL; c < DIM; c++)
                tex[r * DIM + c] = (r << 8) | c;
    }

    printf("\n28. are all four texels in it, weighted as products?\n");
    {
        /*
         * Section 26 walks u with v held still, so it cannot tell a proper
         * 2x2 blend from one that only ever mixes two texels along u, and it
         * says nothing about how the corners are weighted.
         *
         * Give the three channels three of the four corners:
         *
         *      R = 255 where c is odd  and r is even   ->  a(1-b)
         *      G = 255 where c is even and r is odd    ->  (1-a)b
         *      B = 255 where c is odd  and r is odd    ->  ab
         *
         * and walk the diagonal from the centre of texel (4,4) to the centre
         * of (5,5), so a and b run together from 0 to 1.  At the halfway point
         * every weight is a quarter and all three channels must read 63.  A
         * blend that mixes only two texels leaves one of them at 0 throughout.
         */
        long texel = (long)(OSMGA_HW3D_TEX_SPAN / DIM);
        unsigned long r, c;

        for (r = 0UL; r < DIM; r++)
            for (c = 0UL; c < DIM; c++)
                tex[r * DIM + c] =
                      (((c & 1UL) && !(r & 1UL)) ? 0xFFUL     : 0UL)
                    | ((!(c & 1UL) &&  (r & 1UL)) ? 0xFF00UL   : 0UL)
                    | (((c & 1UL) &&  (r & 1UL)) ? 0xFF0000UL : 0UL);

        blank();
        (void)setup(1024UL, 0UL, 33UL, 4UL, texel / 32L,
                    OSMGA_HW3D_TEXF_BILIN);
        batch->state.tmr[1] = 0L;
        batch->state.tmr[2] = texel / 32L;    /* dt/dx: v walks with u */
        batch->state.tmr[3] = 0L;
        batch->state.tmr[6] = 4L * texel + texel / 2L;
        batch->state.tmr[7] = 4L * texel + texel / 2L;
        (void)fire();
        printf("   %4s %6s %6s %6s\n", "col", "R a(1-b)", "G (1-a)b", "B ab");
        for (c = 0UL; c <= 32UL; c += 4UL) {
            unsigned long p = pixat(0UL, c);

            printf("   %4lu %6lu %6lu %6lu\n",
                   c, p & 0xFFUL, (p >> 8) & 0xFFUL, (p >> 16) & 0xFFUL);
        }
        printf("   at column 16 all three must be 63; a zero column is a"
               " corner the engine never fetched\n");

        for (r = 0UL; r < DIM; r++)
            for (c = 0UL; c < DIM; c++)
                tex[r * DIM + c] = (r << 8) | c;
    }

    printf("\n29. and outside the texture altogether?\n");
    {
        /*
         * Section 27 shows the outer half texel holds the edge value, which
         * is as far as a coordinate inside [0,1] can reach.  The kernel
         * allows coordinates well beyond that, so what happens fully outside
         * decides whether CLAMPUV is really GL_CLAMP_TO_EDGE; and both axes
         * outside at once is what would catch the two clamps being applied
         * together rather than one per axis.
         *
         * The interior coordinate has to be a texel CENTRE, not half the
         * texture: half the texture is a texel boundary, where the answer is
         * a blend of two texels and says nothing.  Centre of texel 32 it is.
         *
         * A coordinate below zero is refused by the kernel outright, so the
         * primitive falls back to software and the engine never sees it.
         * That is a safe answer, not a wrong picture, and it is recorded
         * here so that a later change to the coordinate bound does not
         * quietly start accelerating a case nobody measured.
         */
        long texel = (long)(OSMGA_HW3D_TEX_SPAN / DIM);
        long span = (long)OSMGA_HW3D_TEX_SPAN;
        long mid = 32L * texel + texel / 2L;      /* centre of texel 32 */
        long out = span + span / 4L;              /* a quarter past the end */
        long neg = -(span / 4L);
        static const char *name[6] = {
            "u below zero", "u past the end",
            "v below zero", "v past the end",
            "both below",   "both past" };
        long uu[6], vv[6];
        unsigned long want[6];
        int j;

        uu[0] = neg; vv[0] = mid; want[0] = 0x2000UL;
        uu[1] = out; vv[1] = mid; want[1] = 0x203FUL;
        uu[2] = mid; vv[2] = neg; want[2] = 0x0020UL;
        uu[3] = mid; vv[3] = out; want[3] = 0x3F20UL;
        uu[4] = neg; vv[4] = neg; want[4] = 0x0000UL;
        uu[5] = out; vv[5] = out; want[5] = 0x3F3FUL;

        for (j = 0; j < 6; j++) {
            unsigned long got;
            unsigned v;

            blank();
            (void)setup(1024UL, 0UL, 8UL, 4UL, 0L, OSMGA_HW3D_TEXF_BILIN);
            batch->state.tmr[1] = 0L; batch->state.tmr[2] = 0L;
            batch->state.tmr[3] = 0L;
            batch->state.tmr[6] = uu[j];
            batch->state.tmr[7] = vv[j];
            v = fire();
            if (v != OSMGA_HW3D_OK) {
                printf("   %-16s refused (%u) -- software draws it\n",
                       name[j], v);
                continue;
            }
            got = pixat(0UL, 0UL) & 0xFFFFUL;
            printf("   %-16s texel %2lu,%2lu  wanted %2lu,%2lu %s\n",
                   name[j], (got >> 8) & 0xFFUL, got & 0xFFUL,
                   (want[j] >> 8) & 0xFFUL, want[j] & 0xFFUL,
                   (got == want[j]) ? "" : "  <<");
            if (got != want[j]) failures++;
        }
        printf("   clamping to the edge names the nearest edge texel,"
               " one axis at a time\n");
    }

    printf("\n30. where does the destination's alpha come from?\n");
    {
        /*
         * Comparing a textured GL scene against the software path with the
         * alpha byte no longer masked off says the two disagree on EVERY
         * pixel: the software path writes 255 and the engine writes 0.  With
         * GL_REPLACE and an RGB texture the alpha is meant to be the
         * fragment's, and the fragment's is what the encoder programs into
         * ALPHASTART, so something is overriding it.
         *
         * The register reference says why: TDUALSTAGE0 is written as zero,
         * and zero is ALPHA_SEL_ARG1 -- the current texture's alpha -- where
         * ALPHA_SEL_ARG2 with ARG2_DIFFUSE would be the interpolated one.
         * The texture is uploaded from GL_RGB with a zero top byte, so the
         * alpha that wins is zero.
         *
         * That is a reading of a header.  This measures it: give the texture
         * a top byte of 0xAB and the triangle an alpha of 0x55, and see which
         * one lands.
         */
        unsigned long r, c, got;

        for (r = 0UL; r < DIM; r++)
            for (c = 0UL; c < DIM; c++)
                tex[r * DIM + c] = 0xAB000000UL | (r << 8) | c;

        blank();
        {
            OSMGAHW3DTri *t = setup(1024UL, 0UL, 8UL, 4UL, 0L, 0UL);

            batch->state.tmr[1] = 0L; batch->state.tmr[2] = 0L;
            batch->state.tmr[3] = 0L;
            batch->state.tmr[6] = 4L * (long)(OSMGA_HW3D_TEX_SPAN / DIM);
            batch->state.tmr[7] = 4L * (long)(OSMGA_HW3D_TEX_SPAN / DIM);
            t->a0 = 0x55UL << 15;
        }
        (void)fire();
        got = pixat(0UL, 0UL);
        printf("   texture alpha 0xAB, triangle alpha 0x55  ->  pixel %08lx\n",
               got);
        printf("   top byte %02lx: ab is the texture's, 55 is the"
               " fragment's, 00 is neither\n", (got >> 24) & 0xFFUL);

        for (r = 0UL; r < DIM; r++)
            for (c = 0UL; c < DIM; c++)
                tex[r * DIM + c] = (r << 8) | c;
    }

    printf("\n31. and does a VARYING alpha survive?\n");
    {
        /*
         * Section 30 fixes one alpha at one pixel, which proves which operand
         * the stage selects and nothing else.  Selecting the interpolated
         * alpha is only right if the interpolator is actually running: the
         * increments could be ignored, stale, or scaled wrongly and a
         * constant would never say so.
         *
         * So give the triangle a slope -- 0x20 at the left, four a column --
         * and a texture whose own alpha is a value the answer must NOT be.
         */
        unsigned long r, c;
        int bad = 0;

        for (r = 0UL; r < DIM; r++)
            for (c = 0UL; c < DIM; c++)
                tex[r * DIM + c] = 0xAB000000UL | (r << 8) | c;

        blank();
        {
            OSMGAHW3DTri *t = setup(1024UL, 0UL, 16UL, 4UL, 0L, 0UL);

            batch->state.tmr[1] = 0L; batch->state.tmr[2] = 0L;
            batch->state.tmr[3] = 0L;
            batch->state.tmr[6] = 4L * (long)(OSMGA_HW3D_TEX_SPAN / DIM);
            batch->state.tmr[7] = 4L * (long)(OSMGA_HW3D_TEX_SPAN / DIM);
            t->a0  = 0x20UL << 15;
            t->adx = 4UL << 15;
            t->ady = 0UL;
        }
        (void)fire();
        /*
         * EVERY column.  Printing every other one hid the thing that
         * mattered: the end-to-end comparison says the odd columns keep the
         * wrong alpha and the even ones do not, and a stride of two over an
         * even start could never have said so.
         */
        printf("   alpha across the row:");
        for (c = 0UL; c < 16UL; c++) {
            unsigned long got = (pixat(0UL, c) >> 24) & 0xFFUL;

            printf(" %02lx", got);
            if (got != 0x20UL + 4UL * c) bad = 1;
        }
        printf("\n   wanted               ");
        for (c = 0UL; c < 16UL; c++)
            printf(" %02lx", 0x20UL + 4UL * c);
        printf("\n   (the texture's own alpha is ab, so an ab is the texture"
               " winning and a 00 is neither)\n");
        if (bad) {
            printf("   FAIL  %-52s\n",
                   "the interpolated alpha reaches the destination");
            failures++;
        } else
            printf("   ok    %-52s\n",
                   "the interpolated alpha reaches the destination");

        for (r = 0UL; r < DIM; r++)
            for (c = 0UL; c < DIM; c++)
                tex[r * DIM + c] = (r << 8) | c;
    }

    printf("\n32. two lanes, or a story about two lanes?\n");
    {
        /*
         * Section 31 says the even columns take the interpolated alpha and
         * the odd ones take the texture's, which is where the reading came
         * from: TDUALSTAGE0 and TDUALSTAGE1 are not two stages of a serial
         * combiner but one texture-environment word per LANE, and the engine
         * draws the even and odd columns in different lanes.
         *
         * "It works now" would not prove that -- writing anything at all into
         * the second word could have had some other effect.  What proves it
         * is the parity REVERSING when the two words are exchanged, which no
         * other explanation predicts.  The kernel will exchange them on ask;
         * the flags choose between its own two constants and carry nothing
         * into a register.
         */
        static const struct { unsigned long f; const char *name; } cases[3] = {
            { 0UL,                        "both lanes, diffuse" },
            { OSMGA_HW3D_TEXF_TDS1ZERO,   "lane 1 left at zero" },
            { OSMGA_HW3D_TEXF_TDSSWAP,    "the two exchanged" }
        };
        unsigned long r, c;
        int j;

        for (r = 0UL; r < DIM; r++)
            for (c = 0UL; c < DIM; c++)
                tex[r * DIM + c] = 0xAB000000UL | (r << 8) | c;

        for (j = 0; j < 3; j++) {
            int evenOK = 1, oddOK = 1;

            blank();
            {
                OSMGAHW3DTri *t = setup(1024UL, 0UL, 16UL, 4UL, 0L,
                                        cases[j].f);

                batch->state.tmr[1] = 0L; batch->state.tmr[2] = 0L;
                batch->state.tmr[3] = 0L;
                batch->state.tmr[6] =
                    4L * (long)(OSMGA_HW3D_TEX_SPAN / DIM);
                batch->state.tmr[7] =
                    4L * (long)(OSMGA_HW3D_TEX_SPAN / DIM);
                t->a0  = 0x20UL << 15;
                t->adx = 4UL << 15;
                t->ady = 0UL;
            }
            (void)fire();
            printf("   %-20s", cases[j].name);
            for (c = 0UL; c < 12UL; c++) {
                unsigned long got = (pixat(0UL, c) >> 24) & 0xFFUL;

                printf(" %02lx", got);
                if (got != 0x20UL + 4UL * c) {
                    if (c & 1UL) oddOK = 0; else evenOK = 0;
                }
            }
            printf("   even %s, odd %s\n", evenOK ? "ok" : "no",
                   oddOK ? "ok" : "no");
        }
        printf("   the reading needs the third line to be the second one's"
               " mirror; anything else and it is a story\n");

        for (r = 0UL; r < DIM; r++)
            for (c = 0UL; c < DIM; c++)
                tex[r * DIM + c] = (r << 8) | c;
    }

    printf("\n33. are the lanes fixed to the screen, or to the primitive?\n");
    {
        /*
         * Section 32 proves there are two lanes and which register belongs to
         * which, but not what decides a pixel's lane.  It matters: the
         * texture coordinate has a fine structure that also turns on column
         * parity, and whether that parity is counted from the screen or from
         * the primitive's own left edge is the difference between one rule
         * and another.
         *
         * The alpha is now a LANE MARKER.  With the second lane's word left
         * at zero, a pixel drawn by lane 1 keeps the texture's alpha of 0xAB
         * and a pixel drawn by lane 0 takes the interpolated one.  So draw
         * the same thing at four different left edges and see whether the
         * 0xAB columns stay on the odd SCREEN positions or follow the
         * primitive.
         */
        unsigned long r, c;
        long x0;

        for (r = 0UL; r < DIM; r++)
            for (c = 0UL; c < DIM; c++)
                tex[r * DIM + c] = 0xAB000000UL | (r << 8) | c;

        for (x0 = 0L; x0 < 4L; x0++) {
            blank();
            {
                OSMGAHW3DTri *t = setup(1024UL, (unsigned long)x0, 12UL, 4UL,
                                        0L, OSMGA_HW3D_TEXF_TDS1ZERO);

                batch->state.tmr[1] = 0L; batch->state.tmr[2] = 0L;
                batch->state.tmr[3] = 0L;
                batch->state.tmr[6] =
                    4L * (long)(OSMGA_HW3D_TEX_SPAN / DIM);
                batch->state.tmr[7] =
                    4L * (long)(OSMGA_HW3D_TEX_SPAN / DIM);
                t->a0  = 0x20UL << 15;
                t->adx = 0UL;
                t->ady = 0UL;
            }
            (void)fire();
            printf("   left edge %ld, screen x %ld..%ld:  ",
                   x0, x0, x0 + 11L);
            for (c = 0UL; c < 12UL; c++)
                printf("%c", (((pixat(0UL, (unsigned long)x0 + c) >> 24)
                               & 0xFFUL) == 0xABUL) ? '1' : '.');
            printf("   (1 = lane 1)\n");
        }
        printf("   lanes fixed to the screen keep the 1s on odd screen x;"
               " fixed to the primitive they start at the same place\n");

        for (r = 0UL; r < DIM; r++)
            for (c = 0UL; c < DIM; c++)
                tex[r * DIM + c] = (r << 8) | c;
    }

    printf("\n34. is the coordinate's parity the same parity?\n");
    {
        /*
         * The lanes are fixed to the screen (section 33).  The texture
         * coordinate has a fine structure that turns on column parity, and if
         * that parity is the SAME parity then the two anomalies are one thing
         * and the mechanism has a name; if it moves with the primitive
         * instead, they are two.
         *
         * Same programmed start, same increment, one drawing at left edge
         * nought and one at left edge one.  With an increment of one, a
         * column is worth one coordinate unit, so the turnover column is
         * pinned to a single unit.
         *
         *      the lanes' parity   the turnover stays on the same SCREEN x
         *      the primitive's     it stays on the same relative column
         */
        long texel = (long)(OSMGA_HW3D_TEX_SPAN / DIM);
        long tgt = 33L * texel;
        long begin = tgt - 496L - 20L;
        long x0;

        for (x0 = 0L; x0 < 2L; x0++) {
            unsigned long c;
            long first = -1L;

            blank();
            (void)setup(1024UL, (unsigned long)x0, 32UL, 4UL, 1L, 0UL);
            batch->state.tmr[1] = 0L; batch->state.tmr[2] = 0L;
            batch->state.tmr[3] = 0L;
            batch->state.tmr[6] = begin + OSMGA_HW3D_TEX_BIAS;
            batch->state.tmr[7] = 0L;
            (void)fire();
            printf("   left edge %ld  ", x0);
            for (c = 0UL; c < 32UL; c++) {
                unsigned long got =
                    pixat(0UL, (unsigned long)x0 + c) & 0xFFUL;

                printf("%c", (got >= 33UL) ? '1' : '0');
                if (first < 0L && got >= 33UL) first = (long)c;
            }
            printf("   turns at column %ld, screen x %ld\n",
                   first, first + x0);
        }
        printf("   the same screen x twice means the coordinate's parity IS"
               " the lanes'\n");
    }

    printf("\n%s (%d failing)\n",
           failures ? "=== PROBLEM ===" : "=== nothing to report ===", failures);
    return failures ? 1 : 0;
}
