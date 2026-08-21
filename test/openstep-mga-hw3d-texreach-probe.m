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
        say("an empty span with a coordinate past the bound", fire(),
            OSMGA_HW3D_E_TEXCOORD);
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

    printf("\n6b. the same shape with TMR6 = 0, so the coordinate goes NEGATIVE\n");
    blank();
    {
        OSMGAHW3DTri *t = setup(1024UL, 40UL, 8UL, 32UL,
                                (long)(OSMGA_HW3D_TEX_SPAN / DIM), 0UL);
        unsigned long lo, hi, mid, texDirty = 0UL;

        t->ar0 = 32L;
        t->ar2 = -32L;
        t->ar1 = -1L;
        t->sgn = 0x2L;
        /* TMR6 stays zero: every pixel left of the row-0 left edge then has a
         * coordinate below zero, which the validator's non-negative rule was
         * written to keep out -- and which it does not see, because it looks
         * at offsets from the box's left, not from the primitive's origin. */
        say("a left-opening triangle with a zero start", fire(), OSMGA_HW3D_OK);
        lo  = colour[31UL * STRIDE_DW +  9UL];
        mid = colour[31UL * STRIDE_DW + 40UL];
        hi  = colour[31UL * STRIDE_DW + 47UL];
        printf("         bottom row (v,u): x=9 -> (%lu,%lu), x=40 -> (%lu,%lu),"
               " x=47 -> (%lu,%lu)\n",
               lo >> 8, lo & 0xFFUL, mid >> 8, mid & 0xFFUL,
               hi >> 8, hi & 0xFFUL);
        printf("         a clamped negative coordinate reads texel 0\n");
        for (r = 0UL; r < DIM; r++)
            for (c = 0UL; c < DIM; c++)
                if (tex[r * DIM + c] != ((r << 8) | c)) texDirty++;
        printf("         texture words changed by the draw: %lu\n", texDirty);
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

    printf("\n%s (%d failing)\n",
           failures ? "=== PROBLEM ===" : "=== nothing to report ===", failures);
    return failures ? 1 : 0;
}
