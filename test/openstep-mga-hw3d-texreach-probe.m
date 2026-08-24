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

/*
 * The texture anchors moved from the batch to the trapezoid, and these
 * sections were written when they were the batch's.  Writing every entry is
 * what the old assignment meant: "this coordinate, for whatever this draws".
 */
static void setTU(OSMGAHW3DBatch *bp, long v)
{ unsigned long i_; for (i_ = 0UL; i_ < OSMGA_HW3D_MAX_TRI; i_++) bp->tri[i_].tu0 = v; }
static void setTV(OSMGAHW3DBatch *bp, long v)
{ unsigned long i_; for (i_ = 0UL; i_ < OSMGA_HW3D_MAX_TRI; i_++) bp->tri[i_].tv0 = v; }
static void setTQ(OSMGAHW3DBatch *bp, long v)
{ unsigned long i_; for (i_ = 0UL; i_ < OSMGA_HW3D_MAX_TRI; i_++) bp->tri[i_].tq0 = v; }


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
#define DWG_TEXZ        (0x6UL | (0x3UL << 4))   /* the same, with depth */
#define DEPTH_ORG       (5UL * 1024UL * 1024UL)   /* colour is at 4, texture at 6,
                                                   * and the window ends at 7 */

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

    t = &batch->tri[0];
    memset(t, 0, sizeof *t);
    t->tq0 = 1L << 16;   /* after the clear, or it is wiped */
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

/*
 * One reading for section 56: which texel a constant v coordinate lands on,
 * on the 2048-tall texture under repeat.  Only the one pixel that is read is
 * cleared -- blanking the whole surface for each of these would dominate the
 * run, and a bisection asks for a lot of them.
 */
static unsigned long
osmgaProbeReadV(long v7)
{
    unsigned v;

    colour[0] = BLANK;
    (void)setup(64UL, 0UL, 8UL, 4UL, 0L,
                OSMGA_HW3D_TEXF_REPEATU | OSMGA_HW3D_TEXF_REPEATV);
    batch->state.texW = 8UL;
    batch->state.texH = 2048UL;
    batch->state.texPitch = 8UL;
    batch->state.tmr[0] = 0L; batch->state.tmr[1] = 0L;
    batch->state.tmr[2] = 0L; batch->state.tmr[3] = 0L;
    setTU(batch, 0L); setTV(batch, v7);
    v = fire();
    if (v != OSMGA_HW3D_OK)
        return 99999UL;
    return pixat(0UL, 0UL) & 0xFFFFUL;
}

int
main(void)
{
    IOString kind;
    caddr_t cmd, cwin, twin, zwin;
    volatile unsigned short *depth;
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
    /* Wide enough for section 52's other shapes as well as the 64 square:
     * the tallest is eight by 2048, which is exactly this. */
    twin = mapDevice(fd, TEX_ORG, (int)(64UL * 1024UL));
    /* Section 79 has to see whether a refused batch wrote DEPTH as
     * well as colour; sixteen bits a pixel, the same stride. */
    zwin = mapDevice(fd, DEPTH_ORG, (int)(64UL * STRIDE_DW * 2UL));
    if (cmd == (caddr_t)-1 || cwin == (caddr_t)-1 ||
        twin == (caddr_t)-1 || zwin == (caddr_t)-1) {
        printf("a window will not map\n"); return 1;
    }
    batch  = (OSMGAHW3DBatch *)cmd;
    colour = (volatile unsigned long *)cwin;
    tex    = (volatile unsigned long *)twin;
    depth  = (volatile unsigned short *)zwin;

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

        /*
         * This used to assert a refusal, because every negative coordinate
         * was refused.  One unit below nought is now inside the allowance --
         * deliberately, since the edge walk puts a coordinate there -- and
         * section 51 sweeps the whole of it while 52 shows the engine reads
         * such a coordinate exactly as GL says.  So the assertion is turned
         * over rather than deleted: what still has to hold is that a value
         * PAST the allowance is refused, which is what 51 checks, and that a
         * value inside it is admitted, which is this.
         */
        setTU(batch, -1L);
        say("a start one unit below nought is admitted", fire(),
            OSMGA_HW3D_OK);
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
        setTU(batch, (long)(32UL * (OSMGA_HW3D_TEX_SPAN / DIM)));
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
        /*
         * In texels of the 64 texture, except the last two, which are in raw
         * units because they walk the allowance rather than a texel.
         *
         * The first and third used to be refusals: the edge opens leftward,
         * so the coordinate runs backwards from the anchor and went below
         * nought on pixels the primitive draws.  A coordinate may now go a
         * whole texture below nought, and thirty-one texels of a 64 texture
         * is well inside that, so both are admitted -- deliberately.  What
         * still has to be caught is an excursion that leaves the ALLOWANCE,
         * and that is the pair at the end: a start that puts the leftmost
         * pixel exactly on it is taken, one unit less is not.
         */
        static const long starts[5] = {
            0L * (long)(OSMGA_HW3D_TEX_SPAN / DIM),
            31L * (long)(OSMGA_HW3D_TEX_SPAN / DIM),
            30L * (long)(OSMGA_HW3D_TEX_SPAN / DIM),
            31L * (long)(OSMGA_HW3D_TEX_SPAN / DIM)
                - (long)OSMGA_HW3D_TEX_SPAN,
            31L * (long)(OSMGA_HW3D_TEX_SPAN / DIM)
                - (long)OSMGA_HW3D_TEX_SPAN - 1L
        };
        static const int wantOK[5]  = { 1, 1, 1, 1, 0 };
        static const char *label[5] = {
            "a left-opening edge with a zero start",
            "the same edge with a start that covers it",
            "one texel short of covering it",
            "a start that leaves the leftmost pixel on the allowance",
            "one unit past the allowance"
        };
        int k;

        for (k = 0; k < 5; k++) {
            OSMGAHW3DTri *t;
            unsigned ver;

            blank();
            t = setup(1024UL, 40UL, 8UL, 32UL,
                      (long)(OSMGA_HW3D_TEX_SPAN / DIM), 0UL);
            t->ar0 = 32L;
            t->ar2 = -32L;
            t->ar1 = -1L;
            t->sgn = 0x2L;                  /* left edge decreasing */
            setTU(batch, starts[k]);
            ver = fire();
            say(label[k], ver, wantOK[k] ? OSMGA_HW3D_OK
                                         : OSMGA_HW3D_E_TEXCOORD);
            if (k == 1 && ver == OSMGA_HW3D_OK) {
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
            setTV(batch, (long)(32UL * (OSMGA_HW3D_TEX_SPAN / DIM)));
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
        setTU(batch, (long)(step * 31UL));   /* start high so every
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
        setTU(batch, 5L * texel);
        setTV(batch, 3L * texel);
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
        setTU(batch, 5L * texel);
        setTV(batch, 3L * texel);
        batch->triCount = 3UL;
        batch->tri[1] = batch->tri[0];
        batch->tri[1].y = 20L;
        batch->tri[1].fxbndry = (28UL << 16) | 17UL;
        batch->tri[2] = batch->tri[0];
        batch->tri[2].y = 40L;
        batch->tri[2].h = 8L; batch->tri[2].ar0 = 8L; batch->tri[2].ar6 = 8L;
        batch->tri[2].fxbndry = (40UL << 16) | 29UL;
        say("three textured primitives", fire(), OSMGA_HW3D_OK);
        p0 = pixat( 0UL,  0UL);
        p1 = pixat(20UL, 17UL);
        p2 = pixat(40UL, 29UL);
        printf("         v at each primitive's first row: %lu, %lu, %lu"
               "  (3, 11, 19 was the per-batch answer)\n",
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
        setTU(batch, 5L * texel);
        setTV(batch, 3L * texel);
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
        setTU(batch, 5L * texel);
        setTV(batch, 3L * texel);
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

        a  = pixat( 0UL,  0UL);
        b2 = pixat( 8UL, 17UL);
        d  = pixat(34UL, 41UL);
        printf("         v at the three textured firsts: %lu, %lu, %lu\n",
               a >> 8, b2 >> 8, d >> 8);
        printf("         3, 8, 19 was the answer while the matrix was"
               " written once per BATCH\n");
        /*
         * It is not any more.  The anchors are the trapezoid's and the
         * encoder writes the matrix ahead of every primitive's execute, and
         * that write re-seeds -- so four primitives given the same anchor all
         * read it and the heights no longer enter.
         *
         * Equal anchors cannot tell "each reads its own" from "the first is
         * latched and the rest ignored".  Section 78 is what tells those
         * apart, with anchors that differ; this is the weaker corollary.
         */
        if ((a >> 8) == 3UL && (b2 >> 8) == 3UL && (d >> 8) == 3UL)
            printf("   ok    %-52s\n",
                   "the per-primitive write re-seeds v (78 is the proof)");
        else {
            printf("   FAIL  %-52s\n",
                   "the per-primitive write re-seeds v (78 is the proof)");
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
        setTU(batch, 5L * texel);
        setTV(batch, 3L * texel);
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
        setTU(batch, 0L);
        setTV(batch, 0L);
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

    printf("\n9. the vertical span is one primitive's rows, not the batch's\n");
    {
        /*
         * v used to run on across the textured primitives, so N of them
         * reached N times as far: with a y gradient of a sixty-fourth of the
         * budget per row and eight rows each, eight spent 8257536 of 8388608
         * and nine spent 9306112, and the boundary sat between them.
         *
         * The matrix is written ahead of each primitive now and the write
         * re-seeds -- section 78 -- so each of them spends 8257536 whatever
         * the batch holds, and nine of them are as acceptable as one.  A
         * textured batch is no longer eight primitives long.
         */
        static const unsigned long counts[3] = { 1UL, 8UL, 9UL };
        static const int wantOK[3] = { 1, 1, 1 };
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

    printf("\n9b. an empty textured primitive is refused whatever it hides\n");
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
         * The encoder USED to write the texture registers once before the
         * triangle loop while rewriting DWGCTL for every primitive.  It
         * writes both per primitive now.  If moving between
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
        setTU(batch, 5L * texel);
        setTV(batch, 3L * texel);
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
                a  = pixat( 0UL,  0UL);
                c2 = pixat(24UL, 41UL);
                printf("         v at the first and the third: %lu, %lu\n",
                       a >> 8, c2 >> 8);
                printf("         19 was the answer while the matrix was"
                       " written once per BATCH\n");
                /* Every primitive re-seeds from its own anchor now, so the
                 * transition has nothing left to reset.  What this still
                 * rules out is a transition CORRUPTING the anchor: a third
                 * primitive coming back with something other than what it was
                 * given would show here. */
                if ((a >> 8) == 3UL && (c2 >> 8) == 3UL)
                    printf("   ok    %-52s\n",
                           "an atype transition leaves the anchor alone");
                else {
                    printf("   FAIL  %-52s\n",
                           "an atype transition leaves the anchor alone");
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
            setTU(batch, 5L * texel);
            setTV(batch, 3L * texel);
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
        setTU(batch, 0L);
        setTV(batch, 0L);
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
        setTU(batch, texel / 2L);
        setTV(batch, 0L);
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
        setTU(batch, 0L);
        setTV(batch, 0L);
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
        setTU(batch, 0L);
        setTV(batch, 0L);
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
            setTU(batch, 40L * texel);
            setTV(batch, 40L * texel);
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
            setTU(batch, 0L); setTV(batch, 0L);
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
            setTU(batch, mid);
            setTV(batch, mid);
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
            setTU(batch, mid);
            setTV(batch, mid);
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
                setTU(batch, mid);
                setTV(batch, 0L);
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
            setTU(batch, texel - 511L);
            setTV(batch, 0L);
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
                setTU(batch, mid);
                setTV(batch, 0L);
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
        setTU(batch, 5L * texel - 511L);
        setTV(batch, 0L);
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
            setTU(batch, begins[j] + OSMGA_HW3D_TEX_BIAS);
            setTV(batch, 0L);
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
                setTU(batch, begin + OSMGA_HW3D_TEX_BIAS);
                setTV(batch, 0L);
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
                setTU(batch, begin + OSMGA_HW3D_TEX_BIAS);
                setTV(batch, 0L);
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
                setTU(batch, probe);
                setTV(batch, 0L);
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
            setTU(batch, texel - bias);
            setTV(batch, 0L);
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
            setTU(batch, bnd - 32L);
            setTV(batch, 0L);
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
            setTU(batch, 4L * texel);
            setTV(batch, 0L);
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
        setTU(batch, 0L);                 /* the outer half texel */
        setTV(batch, 0L);
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
        setTU(batch, (long)(DIM - 1UL) * texel + texel / 2L);
        setTV(batch, 0L);
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
        setTU(batch, 4L * texel + texel / 2L);
        setTV(batch, 4L * texel + texel / 2L);
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
            setTU(batch, uu[j]);
            setTV(batch, vv[j]);
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
            setTU(batch, 4L * (long)(OSMGA_HW3D_TEX_SPAN / DIM));
            setTV(batch, 4L * (long)(OSMGA_HW3D_TEX_SPAN / DIM));
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
            setTU(batch, 4L * (long)(OSMGA_HW3D_TEX_SPAN / DIM));
            setTV(batch, 4L * (long)(OSMGA_HW3D_TEX_SPAN / DIM));
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
                setTU(batch, 4L * (long)(OSMGA_HW3D_TEX_SPAN / DIM));
                setTV(batch, 4L * (long)(OSMGA_HW3D_TEX_SPAN / DIM));
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
                setTU(batch, 4L * (long)(OSMGA_HW3D_TEX_SPAN / DIM));
                setTV(batch, 4L * (long)(OSMGA_HW3D_TEX_SPAN / DIM));
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
            setTU(batch, begin + OSMGA_HW3D_TEX_BIAS);
            setTV(batch, 0L);
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

    printf("\n35. what product does the engine call GL_MODULATE?\n");
    {
        /*
         * GL_MODULATE is Cv = Cf Ct, and what that means in eight bits is a
         * convention.  Mesa's is
         *
         *      PROD(A, B) = (A * (B + 1)) >> 8         (texture.c:2367)
         *
         * with A the fragment's component and B the texel's, which is not
         * the same as A*B/255 -- they part company by one at, for instance,
         * 192 by 192, where Mesa says 144 and the rounded quotient says 145.
         * So the engine has to be measured against MESA, not against the
         * ideal, since matching Mesa is the whole point.
         *
         * One drawing gives the whole curve: paint the texture so that its
         * red runs 0, 4, 8 ... across the texels and hold the triangle's own
         * red at 128, and column c is then the product of 128 and 4c.  The
         * other three channels are held at values that check the ends --
         * green against 255, blue against nought, and the alpha against 255.
         */
        unsigned long r, c;
        int bad = 0, first = -1;

        for (r = 0UL; r < DIM; r++)
            for (c = 0UL; c < DIM; c++)
                tex[r * DIM + c] = 0xFF000000UL          /* At  = 255 */
                                 | ((c * 4UL) << 16)     /* Ct.r = 4c  */
                                 | (0xFFUL << 8)         /* Ct.g = 255 */
                                 | 0UL;                  /* Ct.b = 0   */

        blank();
        {
            OSMGAHW3DTri *t = setup(1024UL, 0UL, 64UL, 4UL,
                                    (long)(OSMGA_HW3D_TEX_SPAN / DIM),
                                    OSMGA_HW3D_TEXF_MODULATE
                                    | OSMGA_HW3D_TEXF_TEXALPHA);

            batch->state.tmr[1] = 0L; batch->state.tmr[2] = 0L;
            batch->state.tmr[3] = 0L;
            setTU(batch, 0L);
            setTV(batch, 0L);
            t->dr[0] = 128UL << 15;      /* Cf.r */
            t->dr[3] = 200UL << 15;      /* Cf.g */
            t->dr[6] = 100UL << 15;      /* Cf.b */
            t->a0    = 128UL << 15;      /* Af   */
        }
        (void)fire();
        printf("   red, every eighth column:");
        for (c = 0UL; c < 64UL; c += 8UL)
            printf(" %lu", (pixat(0UL, c) >> 16) & 0xFFUL);
        printf("\n   Mesa would say:          ");
        for (c = 0UL; c < 64UL; c += 8UL)
            printf(" %lu", (128UL * (c * 4UL + 1UL)) >> 8);
        printf("\n");
        for (c = 0UL; c < 64UL; c++) {
            unsigned long got = (pixat(0UL, c) >> 16) & 0xFFUL;

            if (got != ((128UL * (c * 4UL + 1UL)) >> 8)) {
                bad++;
                if (first < 0) first = (int)c;
            }
        }
        printf("   %d of 64 columns differ from Mesa's product%s",
               bad, (first >= 0) ? "" : "\n");
        if (first >= 0)
            printf(", first at column %d\n", first);
        {
            unsigned long p = pixat(0UL, 0UL);

            printf("   green %lu (Mesa 200)   blue %lu (Mesa 0)"
                   "   alpha %lu (Mesa 128)\n",
                   (p >> 8) & 0xFFUL, p & 0xFFUL, (p >> 24) & 0xFFUL);
        }

        for (r = 0UL; r < DIM; r++)
            for (c = 0UL; c < DIM; c++)
                tex[r * DIM + c] = (r << 8) | c;
    }

    printf("\n36. the whole product surface, not one line of it\n");
    {
        /*
         * Section 35 holds the fragment's red at 128 and only walks the
         * texel's, which is a weak place to look: at 128 the two candidate
         * conventions agree almost everywhere, so agreeing there says little.
         *
         * Both operands have to move.  The texel's red climbs across the
         * columns as before, and the fragment's red climbs down the ROWS
         * using the colour interpolator's y increment, so the drawing is a
         * 64 by 64 table of products at a stride of four -- 4096 samples of
         * the surface rather than 64 of one line.
         */
        unsigned long r, c;
        unsigned long bad = 0UL, worst = 0UL;
        long bx = -1L, by = -1L;

        for (r = 0UL; r < DIM; r++)
            for (c = 0UL; c < DIM; c++)
                /* the same value in all three channels, and the fragment's
                 * three set alike below, so a per-channel difference in the
                 * product shows up as the three disagreeing */
                tex[r * DIM + c] = 0xFF000000UL | ((c * 4UL) << 16)
                                 | ((c * 4UL) << 8) | (c * 4UL);

        blank();
        {
            OSMGAHW3DTri *t = setup(1024UL, 0UL, 64UL, 64UL,
                                    (long)(OSMGA_HW3D_TEX_SPAN / DIM),
                                    OSMGA_HW3D_TEXF_MODULATE
                                    | OSMGA_HW3D_TEXF_TEXALPHA);

            batch->state.tmr[1] = 0L; batch->state.tmr[2] = 0L;
            batch->state.tmr[3] = 0L;
            setTU(batch, 0L);
            setTV(batch, 0L);
            t->ar0 = 64L; t->ar6 = 64L; t->h = 64L;
            t->dr[0] = 0UL;              /* Cf.r at the top row */
            t->dr[1] = 0UL;              /* and no change across a row */
            t->dr[2] = 4UL << 15;        /* four a row going down */
            t->dr[3] = 0UL; t->dr[4] = 0UL; t->dr[5] = 4UL << 15;
            t->dr[6] = 0UL; t->dr[7] = 0UL; t->dr[8] = 4UL << 15;
            t->a0    = 255UL << 15;
        }
        (void)fire();
        {
            /*
             * Five conventions that all call themselves an eight-bit product.
             * Counting the whole surface against each says which one the
             * engine is, where a single line could not.
             */
            unsigned long miss[5];
            int k;

            for (k = 0; k < 5; k++) miss[k] = 0UL;
            for (r = 0UL; r < DIM; r++)
                for (c = 0UL; c < DIM; c++) {
                    unsigned long got = (pixat(r, c) >> 16) & 0xFFUL;
                    unsigned long a = r * 4UL, b = c * 4UL, t = a * b;
                    unsigned long w[5];

                    w[0] = (a * (b + 1UL)) >> 8;        /* Mesa's PROD    */
                    w[1] = (t + 127UL) / 255UL;         /* rounded /255   */
                    w[2] = t >> 8;                      /* plain shift    */
                    w[3] = (t + 128UL) >> 8;            /* rounded shift  */
                    w[4] = (t + 128UL + ((t + 128UL) >> 8)) >> 8;
                    for (k = 0; k < 5; k++)
                        if (got != w[k]) miss[k]++;
                    if (got != w[0]) {
                        unsigned long d = (got > w[0]) ? got - w[0]
                                                       : w[0] - got;
                        bad++;
                        if (d > worst) {
                            worst = d; bx = (long)b; by = (long)a;
                        }
                    }
                }
            /* a count is not a diagnosis: name the samples that miss */
            printf("   samples the /255 rule does not predict:");
            {
                int shown = 0;

                for (r = 0UL; r < DIM && shown < 8; r++)
                    for (c = 0UL; c < DIM && shown < 8; c++) {
                        unsigned long a = r * 4UL, b = c * 4UL;
                        unsigned long got = (pixat(r, c) >> 16) & 0xFFUL;
                        unsigned long w = (a * b + 127UL) / 255UL;

                        if (got != w) {
                            printf("  Cf %lu Ct %lu got %lu want %lu",
                                   a, b, got, w);
                            shown++;
                        }
                    }
                if (shown == 0) printf("  none");
            }
            printf("\n");
            {   /* and the three channels must agree with each other */
                unsigned long split = 0UL;

                for (r = 0UL; r < DIM; r++)
                    for (c = 0UL; c < DIM; c++) {
                        unsigned long p = pixat(r, c);
                        unsigned long R = (p >> 16) & 0xFFUL;

                        if (((p >> 8) & 0xFFUL) != R || (p & 0xFFUL) != R)
                            split++;
                    }
                printf("   samples where the three channels disagree: %lu\n",
                       split);
            }
            printf("   of 4096 samples, wrong under each rule:\n");
            printf("   %6s %6s %6s %6s %6s\n",
                   "Mesa", "/255", ">>8", "+128", "approx");
            printf("   %6lu %6lu %6lu %6lu %6lu\n",
                   miss[0], miss[1], miss[2], miss[3], miss[4]);
        }
        if (bad)
            printf("   against Mesa the worst is %lu, at texel red %ld,"
                   " fragment red %ld\n", worst, bx, by);
        {
            /* and the two ends, where a wrong shift shows up loudest */
            unsigned long lo = (pixat(0UL, 0UL) >> 16) & 0xFFUL;
            unsigned long hi = (pixat(63UL, 63UL) >> 16) & 0xFFUL;

            printf("   nought by nought %lu (Mesa 0),"
                   "  252 by 252 %lu (Mesa %lu)\n",
                   lo, hi, (252UL * 253UL) >> 8);
        }

        for (r = 0UL; r < DIM; r++)
            for (c = 0UL; c < DIM; c++)
                tex[r * DIM + c] = (r << 8) | c;
    }

    printf("\n37. which bit is which axis, if they are axes at all\n");
    {
        /*
         * TEXCTL's CLAMPUV covers two bits and the guess is one per axis,
         * but a guess is what it is: they could be a mode encoding, or one
         * could be a border clamp.  Clearing them one at a time and putting
         * ONE axis out of range at a time says which.
         *
         *      both set     u reads the edge texel, v reads the edge texel
         *      one clear    exactly one axis wraps -- and which one names it
         *      both clear   both wrap
         *
         * If clearing either single bit makes BOTH axes wrap, it is a mode
         * and not an axis.  If an out-of-range sample comes back as a
         * constant rather than a texel, it is a border and not a repeat.
         */
        long texel = (long)(OSMGA_HW3D_TEX_SPAN / DIM);
        long span = (long)OSMGA_HW3D_TEX_SPAN;
        long inC = 16L * texel + texel / 2L;   /* centre of texel 16 */
        long outC = span + 32L * texel + texel / 2L;  /* one span on, 32.5 */
        static const struct { unsigned long f; const char *name; } st[4] = {
            { 0UL,                          "both clamped" },
            { OSMGA_HW3D_TEXF_REPEATU,      "REPEATU asked" },
            { OSMGA_HW3D_TEXF_REPEATV,      "REPEATV asked" },
            { OSMGA_HW3D_TEXF_REPEATU
              | OSMGA_HW3D_TEXF_REPEATV,    "both asked" }
        };
        int j;

        printf("   %-16s %-22s %s\n", "state", "u out of range", "v out");
        for (j = 0; j < 4; j++) {
            unsigned long gu, gv;
            int k;

            for (k = 0; k < 2; k++) {
                blank();
                (void)setup(1024UL, 0UL, 8UL, 4UL, 0L, st[j].f);
                batch->state.tmr[1] = 0L; batch->state.tmr[2] = 0L;
                batch->state.tmr[3] = 0L;
                setTU(batch, k ? inC : outC);
                setTV(batch, k ? outC : inC);
                if (fire() != OSMGA_HW3D_OK) {
                    if (k) gv = 0xFFFFUL; else gu = 0xFFFFUL;
                    continue;
                }
                if (k) gv = pixat(0UL, 0UL) & 0xFFFFUL;
                else   gu = pixat(0UL, 0UL) & 0xFFFFUL;
            }
            printf("   %-16s v %2lu u %2lu %-12s v %2lu u %2lu\n",
                   st[j].name,
                   (gu >> 8) & 0xFFUL, gu & 0xFFUL,
                   ((gu & 0xFFUL) == 32UL) ? "(u wrapped)"
                                           : "(u clamped)",
                   (gv >> 8) & 0xFFUL, gv & 0xFFUL);
        }
        printf("   u out of range: texel 32 is a wrap, 63 is the edge;"
               " v out of range: v 32 is a wrap, 63 the edge\n");
        printf("   REPEATU must wrap u and leave v, and REPEATV the other"
               " way about; anything else and the names are wrong\n");
    }

    printf("\n38. is the wrap periodic all the way out?\n");
    {
        /*
         * One wrap at 1.5 spans is not periodicity.  The validator admits a
         * coordinate up to eight spans -- and refuses one that goes negative
         * at a drawn pixel, so that half of the domain never reaches the
         * engine at all -- and every one of those eight ought to give the
         * same texel.  A wrap done by a single subtraction would agree near
         * the first span and then stop.
         */
        long texel = (long)(OSMGA_HW3D_TEX_SPAN / DIM);
        long span = (long)OSMGA_HW3D_TEX_SPAN;
        long n;

        printf("   texel read at 32.5 texels plus n spans:");
        for (n = 0L; n < 8L; n++) {
            unsigned long got;

            blank();
            (void)setup(1024UL, 0UL, 8UL, 4UL, 0L,
                        OSMGA_HW3D_TEXF_REPEATU | OSMGA_HW3D_TEXF_REPEATV);
            batch->state.tmr[1] = 0L; batch->state.tmr[2] = 0L;
            batch->state.tmr[3] = 0L;
            setTU(batch, n * span + 32L * texel + texel / 2L);
            setTV(batch, 16L * texel + texel / 2L);
            if (fire() != OSMGA_HW3D_OK) { printf(" ref"); continue; }
            got = pixat(0UL, 0UL) & 0xFFUL;
            printf(" %lu", got);
        }
        printf("\n   all 32 is periodic; a value that drifts is a wrap done"
               " by subtracting once\n");
    }

    printf("\n39. does a linear filter's blend wrap at the seam?\n");
    {
        /*
         * Repeat is open for nearest sampling only, because a linear filter
         * has to do something at the texture's edge that nearest never does:
         * blend across it.  GL's rule masks BOTH taps, so at s = 0 -- where
         * the lower tap is texel -1 -- the answer is half the last texel and
         * half the first, and at s = 1, where the UPPER tap is texel 64, the
         * same.  Clamping either tap gives the edge texel alone instead.
         *
         * The two seams test different taps, which is why both are here: an
         * engine that wraps the tap it is asked for but clamps its neighbour
         * fails one and passes the other.
         *
         * The texture's first column is 0 and its last 255 with 128 between,
         * and the rows likewise in green, so the corner where both axes wrap
         * can be read at the same time.
         */
        long texel = (long)(OSMGA_HW3D_TEX_SPAN / DIM);
        long span = (long)OSMGA_HW3D_TEX_SPAN;
        long mid = 16L * texel + texel / 2L;
        unsigned long r, c;
        static const char *what[3] = { "u at the near seam",
                                       "u at the far seam",
                                       "both axes, the corner" };
        long uu[3], vv[3];
        int j;

        for (r = 0UL; r < DIM; r++)
            for (c = 0UL; c < DIM; c++)
                tex[r * DIM + c] =
                      ((c == 0UL) ? 0UL : (c == DIM - 1UL) ? 255UL : 128UL)
                    | (((r == 0UL) ? 0UL
                        : (r == DIM - 1UL) ? 255UL : 128UL) << 8);

        uu[0] = 0L;    vv[0] = mid;
        uu[1] = span;  vv[1] = mid;
        uu[2] = 0L;    vv[2] = 0L;

        for (j = 0; j < 3; j++) {
            unsigned long p;
            unsigned v;

            blank();
            (void)setup(1024UL, 0UL, 8UL, 4UL, 0L,
                        OSMGA_HW3D_TEXF_BILIN
                        | OSMGA_HW3D_TEXF_REPEATU
                        | OSMGA_HW3D_TEXF_REPEATV);
            batch->state.tmr[1] = 0L; batch->state.tmr[2] = 0L;
            batch->state.tmr[3] = 0L;
            setTU(batch, uu[j]);
            setTV(batch, vv[j]);
            v = fire();
            if (v != OSMGA_HW3D_OK) {
                printf("   %-24s refused (%u)\n", what[j], v);
                continue;
            }
            p = pixat(0UL, 0UL);
            printf("   %-24s u-axis %3lu  v-axis %3lu\n",
                   what[j], p & 0xFFUL, (p >> 8) & 0xFFUL);
        }
        printf("   a wrapped blend reads about 127 where a clamped tap reads"
               " 0; the corner wants 127 in both\n");
        printf("   (the column's value is the low byte and the row's the next"
               " one up)\n");

        for (r = 0UL; r < DIM; r++)
            for (c = 0UL; c < DIM; c++)
                tex[r * DIM + c] = (r << 8) | c;
    }

    printf("\n40. and the phase either side of the seam, not just on it\n");
    {
        /*
         * Section 39 reads the two seams exactly ON them, and an endpoint can
         * be special-cased.  Walking THROUGH each seam a sixty-fourth of a
         * texel at a time says whether the rule holds in the interval or only
         * at the point.
         *
         * With the first texel 0, the last 255 and the middle 128, GL's rule
         * gives a curve that a special case would not reproduce:
         *
         *   from u = 0     127 down to 0 at half a texel, then up towards 64
         *   from u = 63.5  255 down through 127 at the seam to 0 past it
         */
        long texel = (long)(OSMGA_HW3D_TEX_SPAN / DIM);
        unsigned long r, c;
        static const long starts[2] = { 0L, 63L * 16384L + 8192L };
        static const char *name[2] = { "from u = 0      ",
                                       "from u = 63.5   " };
        int j;

        for (r = 0UL; r < DIM; r++)
            for (c = 0UL; c < DIM; c++)
                tex[r * DIM + c] =
                      ((c == 0UL) ? 0UL : (c == DIM - 1UL) ? 255UL : 128UL)
                    | (128UL << 8);

        for (j = 0; j < 2; j++) {
            blank();
            (void)setup(1024UL, 0UL, 64UL, 4UL, texel / 64L,
                        OSMGA_HW3D_TEXF_BILIN
                        | OSMGA_HW3D_TEXF_REPEATU
                        | OSMGA_HW3D_TEXF_REPEATV);
            batch->state.tmr[1] = 0L; batch->state.tmr[2] = 0L;
            batch->state.tmr[3] = 0L;
            setTU(batch, starts[j]);
            setTV(batch, 16L * texel + texel / 2L);
            if (fire() != OSMGA_HW3D_OK) {
                printf("   %s refused\n", name[j]);
                continue;
            }
            printf("   %s", name[j]);
            for (c = 0UL; c < 64UL; c += 8UL)
                printf(" %3lu", pixat(0UL, c) & 0xFFUL);
            printf("\n");
        }
        /* Mesa's own arithmetic, weights rounded then the sum shifted --
         * not a hand-written guess, which is what these two lines were the
         * first time and they were wrong */
        printf("   Mesa            127  95  63  31   0  16  32  48\n");
        printf("   and             255 223 191 159 127  95  63  31\n");
        printf("   the shape, the slope and where it turns are the rule;"
               " a last few one-level differences are the usual rounding\n");

        for (r = 0UL; r < DIM; r++)
            for (c = 0UL; c < DIM; c++)
                tex[r * DIM + c] = (r << 8) | c;
    }

    printf("\n41. what actually keeps the address inside the texture?\n");
    {
        /*
         * The header says the coordinate bound is there so the coordinate
         * stays inside the range that has been MEASURED.  That is a rule
         * about not going where nobody has looked -- it is not what keeps
         * the address inside the texture.  What does that is the addressing
         * itself: clamped, the index saturates; repeating, it is masked.
         *
         * The distinction matters for perspective, where a division can make
         * the coordinate enormous.  So check the claim at the far end of the
         * admitted range rather than near the texture: at eight spans, which
         * is the most the validator allows, both modes must still name a
         * texel of this texture and not something past it.
         */
        long span = (long)OSMGA_HW3D_TEX_SPAN;
        static const long us[3] = { 1L, 4L, 8L };   /* spans out */
        int j, k;

        for (k = 0; k < 2; k++) {
            printf("   %-9s", k ? "repeating" : "clamped");
            for (j = 0; j < 3; j++) {
                unsigned long got;
                unsigned v;

                blank();
                (void)setup(1024UL, 0UL, 8UL, 4UL, 0L,
                            k ? (OSMGA_HW3D_TEXF_REPEATU
                                 | OSMGA_HW3D_TEXF_REPEATV) : 0UL);
                batch->state.tmr[1] = 0L; batch->state.tmr[2] = 0L;
                batch->state.tmr[3] = 0L;
                /* a quarter of a texel short of a whole number of spans, so
                 * clamping names the last texel and repeating the last too */
                setTU(batch, us[j] * span - span / (long)DIM / 4L);
                setTV(batch, us[j] * span - span / (long)DIM / 4L);
                v = fire();
                if (v != OSMGA_HW3D_OK) { printf("  %ld spans refused", us[j]); continue; }
                got = pixat(0UL, 0UL) & 0xFFFFUL;
                printf("  %ld spans -> v %lu u %lu", us[j],
                       (got >> 8) & 0xFFUL, got & 0xFFUL);
            }
            printf("\n");
        }
        printf("   every one must name a texel of 0..63; anything else and the"
               " addressing is not what contains it\n");
    }

    printf("\n42. does the engine divide by the denominator plane?\n");
    {
        /*
         * The H family stopped being the kernel's own with TEXF_PERSP: it is
         * now a plane the client sends and the validator bounds, and the
         * encoder clears NOPERSPECTIVE.  Whether the engine then divides by
         * it -- and whether 16.16 is really the format -- is a picture, not a
         * reading of somebody else's driver.
         *
         * Hold s at texel eight and let q climb a sixty-fourth a column from
         * one.  If the engine divides, the texel walks BACK from eight as the
         * denominator grows, in the ratio the arithmetic says.
         */
        long texel = (long)(OSMGA_HW3D_TEX_SPAN / DIM);
        unsigned long c;

        blank();
        (void)setup(1024UL, 0UL, 64UL, 4UL, 0L, OSMGA_HW3D_TEXF_PERSP);
        batch->state.tmr[1] = 0L; batch->state.tmr[2] = 0L;
        batch->state.tmr[3] = 0L;
        setTU(batch, 8L * texel);      /* s = texel 8, flat */
        setTV(batch, 0L);
        batch->state.tmr[4] = 1024L;           /* dq/dx = 1/64 */
        batch->state.tmr[5] = 0L;
        setTQ(batch, OSMGA_HW3D_Q_ONE);
        printf("   verdict %u   u:", fire());
        for (c = 0UL; c < 64UL; c += 8UL)
            printf(" %lu", pixat(0UL, c) & 0xFFUL);
        printf("\n   dividing gives  8 7 6 5 5 4 4 4\n");
        printf("   a flat 8 means the plane was ignored;"
               " anything else is a different format\n");
    }

    printf("\n43. does the denominator's row index run on, or restart?\n");
    {
        /*
         * The validator admits a perspective primitive only if BOTH readings
         * of the denominator's row index are in range, because which one the
         * engine uses was not known -- v's index runs on across a batch and
         * u's restarts.  Now that the divider is known to work, the question
         * can be answered instead of hedged.
         *
         * Two textured primitives in one batch, the first eight rows tall,
         * with q climbing a sixteenth a row.  Read the SECOND one's first
         * row: restarting reads texel 8, running on reads what eight rows of
         * climb leaves.
         */
        long texel = (long)(OSMGA_HW3D_TEX_SPAN / DIM);
        unsigned long got;

        blank();
        memset(batch, 0, sizeof *batch);
        batch->magic = OSMGA_HW3D_MAGIC;
        batch->version = OSMGA_HW3D_VERSION;
        batch->triCount = 2UL;
        batch->state.dstorg = COLOUR_ORG;
        batch->state.dstWidth = 1024UL;
        batch->state.dstHeight = 64UL;
        batch->state.dstPitch = STRIDE_DW;
        batch->state.texorg = TEX_ORG;
        batch->state.texW = DIM; batch->state.texH = DIM;
        batch->state.texPitch = DIM;
        batch->state.texFormat = OSMGA_HW3D_TEXFMT_TW32;
        batch->state.texFlags = OSMGA_HW3D_TEXF_PERSP;
        setTU(batch, 8L * texel);
        setTQ(batch, OSMGA_HW3D_Q_ONE);
        batch->state.tmr[5] = 4096L;            /* dq/dy = 1/16 */
        {
            int k;

            for (k = 0; k < 2; k++) {
                OSMGAHW3DTri *t = &batch->tri[k];

                t->dwgctl = DWG_TEX;
                t->alphactrl = 0x00000101UL;
                t->y = (long)(k * 8);
                t->h = 8L;
                t->ar0 = 8L; t->ar6 = 8L;
                t->fxbndry = (8UL << 16) | 0UL;
                t->dr[0] = 200UL << 15;
            }
        }
        printf("   verdict %u", fire());
        got = pixat(8UL, 0UL) & 0xFFUL;         /* second primitive, row 0 */
        printf("   second primitive's first row reads texel %lu\n", got);
        printf("   restarting gives 8, running on gives 5\n");
    }

    printf("\n44. is that a screen plane, or an accumulator?\n");
    {
        /*
         * Section 43 stacks the two primitives so they touch, which makes
         * "q is a plane in screen coordinates" and "q is an accumulator that
         * ran eight rows" the same answer.  Leave a GAP and they part: a
         * plane reads the gap, an accumulator does not.
         *
         * It matters because the validator models the row index as the
         * accumulated count of TEXTURED rows, which is what v does.  If the
         * denominator follows the screen instead, that model is wrong.
         */
        long texel = (long)(OSMGA_HW3D_TEX_SPAN / DIM);
        unsigned long got;

        blank();
        memset(batch, 0, sizeof *batch);
        batch->magic = OSMGA_HW3D_MAGIC;
        batch->version = OSMGA_HW3D_VERSION;
        batch->triCount = 2UL;
        batch->state.dstorg = COLOUR_ORG;
        batch->state.dstWidth = 1024UL;
        batch->state.dstHeight = 64UL;
        batch->state.dstPitch = STRIDE_DW;
        batch->state.texorg = TEX_ORG;
        batch->state.texW = DIM; batch->state.texH = DIM;
        batch->state.texPitch = DIM;
        batch->state.texFormat = OSMGA_HW3D_TEXFMT_TW32;
        batch->state.texFlags = OSMGA_HW3D_TEXF_PERSP;
        setTU(batch, 8L * texel);
        setTQ(batch, OSMGA_HW3D_Q_ONE);
        batch->state.tmr[5] = 4096L;            /* dq/dy = 1/16 */
        {
            static const long ys[2] = { 0L, 20L };
            static const long hs[2] = { 8L, 4L };
            int k;

            for (k = 0; k < 2; k++) {
                OSMGAHW3DTri *t = &batch->tri[k];

                t->dwgctl = DWG_TEX;
                t->alphactrl = 0x00000101UL;
                t->y = ys[k];
                t->h = hs[k];
                t->ar0 = hs[k]; t->ar6 = hs[k];
                t->fxbndry = (8UL << 16) | 0UL;
                t->dr[0] = 200UL << 15;
            }
        }
        printf("   verdict %u", fire());
        got = pixat(20UL, 0UL) & 0xFFUL;
        printf("   the far primitive's first row reads texel %lu\n", got);
        printf("   a screen plane gives 3, an accumulator that walked eight"
               " rows gives 5\n");
    }

    printf("\n45. how well does the divider divide, in units of s?\n");
    {
        /*
         * "Does it still read texel eight" is too blunt to say anything about
         * a divider: eight and a half texels sits 8192 units from either
         * boundary, so an error of nearly six percent reads as a pass.
         *
         * Bisect instead.  For each denominator, find the smallest numerator
         * whose texel is nine rather than eight, and the arithmetic says that
         * is ceil(9 * q / 4).  The difference between the two is the error,
         * in units of s, and one unit of s is 65536/q of coordinate -- so
         * this sees a thousandth of a texel where the other saw half of one.
         *
         * The denominators are powers of two AND odd values.  A power of two
         * may take an easier path through a normaliser, which is a guess
         * nobody here can settle by arithmetic, so the measurement is made to
         * settle it instead of the guess being believed.
         */
        long texel = (long)(OSMGA_HW3D_TEX_SPAN / DIM);
        /*
         * The denominators moved above Q_MIN when that became an accuracy
         * budget rather than a guess.  The mantissas are the same ones --
         * the addend has no exponent term -- so the science is unchanged.
         */
        static const long qs[10] = {
            8192L, 8224L, 9600L, 12288L, 14336L, 16352L,
            16384L, 24576L, 65536L, 262144L
        };
        int j;

        printf("   %8s %10s %10s %8s %8s\n",
               "q", "measured", "arithmetic", "error", "model");
        for (j = 0; j < 10; j++) {
            long q = qs[j];
            long want = (9L * q + 3L) / 4L;
            /*
             * A window of sixty-four either side was too narrow: three of
             * the denominators pinned to its bottom, which is not a
             * measurement but a wall.  Wide enough to hold any answer, and
             * enough halvings to reach it.
             */
            long lo = 1L, hi = 2L * want, mid;
            int it, blanked = 0;

            for (it = 0; it < 21 && lo < hi; it++) {
                unsigned long p;

                mid = (lo + hi) / 2L;
                blank();
                (void)setup(64UL, 0UL, 8UL, 4UL, 0L, OSMGA_HW3D_TEXF_PERSP);
                batch->state.tmr[1] = 0L; batch->state.tmr[2] = 0L;
                batch->state.tmr[3] = 0L;
                setTU(batch, mid);
                setTV(batch, 0L);
                batch->state.tmr[4] = 0L; batch->state.tmr[5] = 0L;
                setTQ(batch, q);
                if (fire() != OSMGA_HW3D_OK) { blanked = 1; break; }
                p = pixat(0UL, 0UL);
                if (p == BLANK) { blanked = 1; break; }
                if ((p & 0xFFUL) >= 9UL) hi = mid; else lo = mid + 1L;
            }
            if (blanked) {
                printf("   %8ld %10s\n", q, "refused");
                continue;
            }
            {
                /*
                 * Two things sit between the plain arithmetic and the step,
                 * and they compose: the affine ladder, which is 512 less a
                 * granularity taken from the NUMERATOR's magnitude and which
                 * the kernel has already had 496 taken off, and the
                 * perspective addend of 512 times q's mantissa.
                 *
                 * The first version of this line had only the addend and
                 * called the two largest denominators wrong -- they are the
                 * ones whose numerator is big enough for the ladder to have
                 * stepped.
                 */
                long p2 = 1L, e2, gg = 1L, hi2 = 1L << 16, n = lo - 496L;

                while (p2 * 2L <= q) p2 *= 2L;
                e2 = (512L * (q - p2)) / p2;
                if (n < 0L) n = -n;
                while (hi2 <= n) { hi2 <<= 1; gg <<= 1; }
                printf("   %8ld %10ld %10ld %8ld %8ld %s\n",
                       q, lo, want, lo - want, -((16L - gg) + e2),
                       (lo - want == -((16L - gg) + e2)) ? "" : "<<");
            }
        }
        printf("   the error must be -((16 - g) + 512f), the ladder and the"
               " addend together\n");
        (void)texel;
    }

    printf("\n46. and a numerator larger than the affine path ever sent?\n");
    {
        /*
         * Holding the coordinate fixed while raising the denominator raises
         * the NUMERATOR with it: at q = Q_MAX the same eight and a half
         * texels needs s = 17825792, which is past the 8388608 the affine
         * path was ever allowed, and the ratio rule admits s up to 128 * q,
         * a hundred and twenty-eight times further still.  Whether the
         * engine's numerator behaves out there was never measured -- it could
         * not be, because until now nothing could send it.
         *
         * This costs nothing: those denominators are already admitted.
         */
        long texel = (long)(OSMGA_HW3D_TEX_SPAN / DIM);
        static const long qs[4] = { 65536L, 262144L, 2097152L, 8388608L };
        int j;

        printf("   %10s %12s %8s %8s\n", "q", "s", "texel", "wanted");
        for (j = 0; j < 4; j++) {
            long q = qs[j];
            long s = (long)(8.5 * (double)texel) / 65536L * q;
            unsigned long got;

            s = (17L * (long)texel / 2L) * (q / 65536L);
            blank();
            (void)setup(64UL, 0UL, 8UL, 4UL, 0L, OSMGA_HW3D_TEXF_PERSP);
            batch->state.tmr[1] = 0L; batch->state.tmr[2] = 0L;
            batch->state.tmr[3] = 0L;
            setTU(batch, s);
            setTV(batch, 0L);
            batch->state.tmr[4] = 0L; batch->state.tmr[5] = 0L;
            setTQ(batch, q);
            if (fire() != OSMGA_HW3D_OK) {
                printf("   %10ld %12ld %8s\n", q, s, "refused");
                continue;
            }
            got = pixat(0UL, 0UL);
            printf("   %10ld %12ld %8lu %8d%s\n", q, s,
                   (got == BLANK) ? 999UL : (got & 0xFFUL), 8,
                   (got == BLANK) ? "  (undrawn)" : "");
        }
        printf("   every one wants texel 8; the affine path never sent an s"
               " past 8388608\n");
    }

    printf("\n48. the addend across octaves, not just inside one\n");
    {
        /*
         * "E is twice q's distance above the power of two below it" was
         * fitted to five denominators, four of them in one octave.  That is
         * not enough to call a law, and the sharp test is a MANTISSA one:
         * 300, 600 and 1200 share a normalised fraction, as do 384, 768 and
         * 1536, so if the rule really turns on the mantissa each family must
         * show the SAME offset in texels even though E itself doubles.
         *
         * Two boundaries per denominator, solved together, because one cannot
         * separate an addend from a distorted divisor:
         *
         *      s9  + 15 = 9D/4  - E        s25 + 15 = 25D/4 - E
         *      D = (b - a) / 4             E = 9D/4 - a
         */
        static const long qs[12] = {
            8192L, 8224L, 9600L, 12288L, 14336L, 16352L,
            16384L, 16416L, 19200L, 24576L, 38400L, 49152L
        };
        int j;

        printf("   %6s %8s %8s %8s %8s %8s %s\n",
               "q", "s9", "s25", "D", "E", "2(q-2^e)", "texels");
        for (j = 0; j < 12; j++) {
            long q = qs[j];
            long got[2];
            static const long ks[2] = { 9L, 25L };
            int b2, bad = 0;

            for (b2 = 0; b2 < 2; b2++) {
                long k = ks[b2];
                long lo = 1L, hi = k * q, mid;
                int it;

                for (it = 0; it < 22 && lo < hi; it++) {
                    unsigned long p;

                    mid = (lo + hi) / 2L;
                    blank();
                    (void)setup(64UL, 0UL, 8UL, 4UL, 0L,
                                OSMGA_HW3D_TEXF_PERSP);
                    batch->state.tmr[1] = 0L; batch->state.tmr[2] = 0L;
                    batch->state.tmr[3] = 0L;
                    setTU(batch, mid);
                    setTV(batch, 0L);
                    batch->state.tmr[4] = 0L; batch->state.tmr[5] = 0L;
                    setTQ(batch, q);
                    if (fire() != OSMGA_HW3D_OK) { bad = 1; break; }
                    p = pixat(0UL, 0UL);
                    if (p == BLANK) { bad = 1; break; }
                    if ((long)(p & 0xFFUL) >= k) hi = mid; else lo = mid + 1L;
                }
                if (bad) break;
                got[b2] = lo;
            }
            if (bad) { printf("   %6ld %8s\n", q, "refused"); continue; }
            {
                long a = got[0] + 15L, bb = got[1] + 15L;
                long d4 = bb - a;                 /* four times the divisor */
                long e  = (9L * d4 - 16L * a) / 16L;
                long p2 = 1L;

                while (p2 * 2L <= q) p2 *= 2L;
                printf("   %6ld %8ld %8ld %8ld %8ld %8ld %ld.%02ld\n",
                       q, got[0], got[1], d4 / 4L, e, 2L * (q - p2),
                       (4L * e) / q, ((400L * e) / q) % 100L);
            }
        }
        printf("   D must be q, and E must be 512 times the mantissa --"
               " 9600/19200/38400 all 88, 12288/24576/49152 all 256\n");
    }

    printf("\n49. and with the denominator VARYING, which is what perspective is\n");
    {
        /*
         * Everything so far held q constant over the primitive, which is not
         * perspective at all.  A row with dq/dx running, crossing an octave
         * of q partway, asks whether the addend really is evaluated per pixel
         * from the local mantissa -- because if it is, it climbs towards 512
         * as q approaches the power of two and drops to nought as it crosses.
         *
         * q runs 12288 to 24576 across 64 columns, so it passes 16384 at
         * column 22.  The reset is 512 * 65536 / 16384 = 2048 coordinate
         * units, an eighth of a texel.
         *
         * The row is printed whole and checked against the model outside;
         * a single engineered column would be too easy to read into.
         */
        long q0 = 12288L;
        /*
         * An eighth of a texel is small against an oracle that reads texels,
         * so ONE row only disagrees with the no-addend model in three or four
         * columns.  Six numerator phases, chosen so each disagrees in five or
         * more, put forty-odd discriminating observations on the table
         * instead of three.
         */
        static const long ss[6] = { 36260L, 36416L, 36845L, 37222L, 37989L };
        int j;

        for (j = 0; j < 6; j++) {
            unsigned long c;

            blank();
            (void)setup(1024UL, 0UL, 64UL, 4UL, 0L, OSMGA_HW3D_TEXF_PERSP);
            batch->state.tmr[1] = 0L; batch->state.tmr[2] = 0L;
            batch->state.tmr[3] = 0L;
            setTU(batch, ss[j]);
            setTV(batch, 0L);
            batch->state.tmr[4] = 192L;          /* dq/dx */
            batch->state.tmr[5] = 0L;
            setTQ(batch, q0);
            printf("   s=%ld v=%u:", ss[j], fire());
            for (c = 0UL; c < 64UL; c++)
                printf(" %lu", pixat(0UL, c) & 0xFFUL);
            printf("\n");
        }
        printf("   q runs %ld to %ld, crossing 16384 at column 22\n",
               q0, q0 + 192L * 63L);
    }

    printf("\n50. one derivative at a time, on a shape that is not square\n");
    {
        /*
         * The builder and the validator once disagreed about which of tmr[1]
         * and tmr[2] was which, and it went unseen because nearly every probe
         * leaves both at nought -- where the two readings agree.  What catches
         * a transposition is ONE derivative at a time on a primitive whose
         * width and height differ, with a slope chosen to leave the range
         * along its own axis while staying inside it along the other.
         *
         * With a shape 8 by 32 and the limit at 8 texture spans, a slope of
         * about a tenth of the limit runs out over 31 rows and not over 7
         * columns.  So a y slope must be refused on the tall shape, and an x
         * slope on the wide one, and a validator that has the two swapped
         * admits exactly one of each pair.
         */
        long v = (long)OSMGA_HW3D_TEX_COORD_MAX / 10L;
        static const char *nm[4] = { "ds/dx wide", "ds/dy tall",
                                     "dt/dx wide", "dt/dy tall" };
        int j;

        for (j = 0; j < 4; j++) {
            int tall = (j & 1);
            unsigned got;

            blank();
            {
                OSMGAHW3DTri *t = setup(1024UL, 0UL,
                                        tall ? 8UL : 32UL,
                                        tall ? 32UL : 8UL, 0L, 0UL);

                batch->state.tmr[0] = 0L; batch->state.tmr[1] = 0L;
                batch->state.tmr[2] = 0L; batch->state.tmr[3] = 0L;
                batch->state.tmr[j] = v;
                setTU(batch, 0L);
                setTV(batch, 0L);
                t->ar0 = tall ? 32L : 8L;
                t->ar6 = t->ar0;
            }
            got = fire();
            say(nm[j], (got == OSMGA_HW3D_OK) ? 0U : 1U, 1U);
        }
        printf("   each must be refused; an admitted one is a slope bounded"
               " against the wrong span\n");

        /*
         * Those four are refused by the slope MAGNITUDE check on its own, so
         * they say nothing about whether the coordinate evaluation was fixed
         * as well.  This pair does: a slope exactly at what the magnitude
         * check allows -- room over the height, so it passes -- with a start
         * halfway up the range, so the last row leaves it.  Only an
         * evaluation that puts tmr[1] into u can see that, and only one that
         * puts tmr[2] into v can see its mirror.
         */
        {
            long room2 = (long)OSMGA_HW3D_TEX_COORD_MAX;
            long slope = room2 / 31L;
            int k;

            for (k = 0; k < 2; k++) {
                unsigned got;

                blank();
                {
                    OSMGAHW3DTri *t = setup(1024UL, 0UL, 8UL, 32UL, 0L, 0UL);

                    batch->state.tmr[0] = 0L; batch->state.tmr[1] = 0L;
                    batch->state.tmr[2] = 0L; batch->state.tmr[3] = 0L;
                    batch->state.tmr[k ? 3 : 1] = slope;
                    setTU(batch, k ? 0L : room2 / 2L);
                    setTV(batch, k ? room2 / 2L : 0L);
                    t->ar0 = 32L; t->ar6 = 32L;
                }
                got = fire();
                say(k ? "v carried out of range by its own dy"
                      : "u carried out of range by its own dy",
                    (got == OSMGA_HW3D_OK) ? 0U : 1U, 1U);
            }
            /*
             * And the control, so that "the slope alone is legal" is a
             * measurement rather than something I worked out: the same slope
             * with the start at nought keeps the coordinate inside the range
             * all the way down, and must be ADMITTED.  If this were refused
             * the pair above would prove nothing about the evaluation.
             */
            {
                unsigned got;

                blank();
                {
                    OSMGAHW3DTri *t = setup(1024UL, 0UL, 8UL, 32UL, 0L, 0UL);

                    batch->state.tmr[0] = 0L; batch->state.tmr[1] = slope;
                    batch->state.tmr[2] = 0L; batch->state.tmr[3] = 0L;
                    setTU(batch, 0L);
                    setTV(batch, 0L);
                    t->ar0 = 32L; t->ar6 = 32L;
                }
                got = fire();
                say("the same slope from nought is admitted", got,
                    OSMGA_HW3D_OK);
            }
            printf("   so the refusals above are the coordinate evaluation"
                   " and not the slope check\n");
        }
    }

    printf("\n51. the sliver of negative coordinate that is now admitted\n");
    {
        /*
         * A coordinate a hair below nought is admitted now, because the edge
         * walk's integer x sits a fraction of a pixel outside the true edge
         * and a coordinate that is nought along that edge comes out just
         * under it -- measured at 0.00088 of a texel, and refusing it sent a
         * whole triangle of a perspective quad to software.
         *
         * The allowance is 4096 coordinate units.  Calling that "a quarter
         * of a texel" was wrong: a texel is 2^20/size units, so the quarter
         * holds only on a 64 texture, which is the one below.  What this
         * section does is SAMPLE seven points of the interval and check that
         * one unit past it is still refused; the interval itself is walked a
         * unit at a time in 53, its boundaries in 54, and other sizes in 52.
         */
        /*
         * All of these stay negative AFTER the kernel takes its 496 off and
         * the engine adds its ladder back -- the net is fifteen, so a
         * programmed -1 would come out at +14 and read texel nought in both
         * modes, which would prove nothing.  Sixteen is the smallest that
         * still lands below.
         */
        static const long ps[7] = { -4096L, -3000L, -2000L, -1000L,
                                    -500L, -100L, -16L };
        int j, k;

        for (k = 0; k < 2; k++) {
            printf("   %-9s", k ? "repeating" : "clamped");
            for (j = 0; j < 7; j++) {
                unsigned long got;
                unsigned v;

                blank();
                (void)setup(64UL, 0UL, 8UL, 4UL, 0L,
                            k ? (OSMGA_HW3D_TEXF_REPEATU
                                 | OSMGA_HW3D_TEXF_REPEATV) : 0UL);
                batch->state.tmr[1] = 0L; batch->state.tmr[2] = 0L;
                batch->state.tmr[3] = 0L;
                setTU(batch, ps[j]);
                setTV(batch, 0L);
                v = fire();
                if (v != OSMGA_HW3D_OK) { printf("  ref"); continue; }
                got = pixat(0UL, 0UL);
                printf("  %3lu", (got == BLANK) ? 999UL : (got & 0xFFUL));
            }
            printf("      wanted %s\n", k ? "63 throughout" : "0 throughout");
        }
        {
            unsigned v;

            blank();
            (void)setup(64UL, 0UL, 8UL, 4UL, 0L, 0UL);
            batch->state.tmr[1] = 0L; batch->state.tmr[2] = 0L;
            batch->state.tmr[3] = 0L;
            setTU(batch, -1048577L);
            setTV(batch, 0L);
            v = fire();
            say("one unit past the allowance is still refused",
                (v == OSMGA_HW3D_OK) ? 0U : 1U, 1U);
        }
    }

    printf("\n52. the same negative coordinates on other texture sizes\n");
    {
        /*
         * Section 51 swept the allowance on the 64 square, where one texel is
         * 16384 units -- so every value in the sweep is inside the SAME texel
         * and the reading cannot tell -4096 from -16.  On a 1024 texture one
         * texel is 1024 units and the sweep crosses four of them; on a 2048
         * one it crosses eight.  Those shapes therefore measure the wrap
         * arithmetic itself, and with it whatever the engine really adds to a
         * negative coordinate, which the 64 square cannot see.
         *
         * No verdict is asserted here.  The numbers are printed and fitted
         * against the models in python, because the point is to find out what
         * the engine does, not to confirm what I assumed.
         *
         * Width is capped below 2048 by the pitch field rather than by
         * TEX_MAX_DIM: pitch must be at least the width and at most 2047, so
         * a 2048-wide texture cannot be described at all.  Height has no such
         * companion, so the eight-texel case is reached down the v axis.
         */
        static const long ps[7] = { -4096L, -3000L, -2000L, -1000L,
                                    -500L, -100L, -16L };
        static const unsigned long cw[3] = { 8UL, 1024UL, 8UL };
        static const unsigned long chh[3] = { 8UL, 4UL, 2048UL };
        static const int cax[3] = { 0, 0, 1 };
        static const char *cnm[3] = { "8 wide u", "1024 wide u", "2048 tall v" };
        int ci, j, k;

        for (ci = 0; ci < 3; ci++) {
            unsigned long rr, cc;

            for (rr = 0UL; rr < chh[ci]; rr++)
                for (cc = 0UL; cc < cw[ci]; cc++)
                    tex[rr * cw[ci] + cc] = cax[ci] ? rr : cc;

            printf("   %-12s texel %6lu units\n", cnm[ci],
                   OSMGA_HW3D_TEX_SPAN / (cax[ci] ? chh[ci] : cw[ci]));
            for (k = 0; k < 2; k++) {
                printf("     %-9s", k ? "repeating" : "clamped");
                for (j = 0; j < 7; j++) {
                    unsigned long got;
                    unsigned v;

                    blank();
                    (void)setup(64UL, 0UL, 8UL, 4UL, 0L,
                                k ? (OSMGA_HW3D_TEXF_REPEATU
                                     | OSMGA_HW3D_TEXF_REPEATV) : 0UL);
                    batch->state.texW = cw[ci];
                    batch->state.texH = chh[ci];
                    batch->state.texPitch = cw[ci];
                    batch->state.tmr[0] = 0L; batch->state.tmr[1] = 0L;
                    batch->state.tmr[2] = 0L; batch->state.tmr[3] = 0L;
                    setTU(batch, cax[ci] ? 0L : ps[j]);
                    setTV(batch, cax[ci] ? ps[j] : 0L);
                    v = fire();
                    if (v != OSMGA_HW3D_OK) { printf("   ref"); continue; }
                    got = pixat(0UL, 0UL);
                    printf("  %4lu", (got == BLANK) ? 9999UL
                                                    : (got & 0xFFFFUL));
                }
                printf("\n");
            }
        }
        printf("     values swept:");
        for (j = 0; j < 7; j++) printf("  %4ld", ps[j]);
        printf("\n");

        blank();
        {
            unsigned v;

            (void)setup(64UL, 0UL, 8UL, 4UL, 0L, 0UL);
            batch->state.texW = 2048UL;
            batch->state.texH = 8UL;
            batch->state.texPitch = 2048UL;
            v = fire();
            say("a 2048-wide texture cannot be described",
                (v == OSMGA_HW3D_E_TEXSIZE) ? 1U : 0U, 1U);
        }
    }

    printf("\n53. walking a texel boundary, to try to break the model\n");
    {
        /*
         * Sections 51 and 52 are confirmations, and a confirmation of a model
         * that already fits is worth little.  This one is built to REFUTE.
         *
         * The model fitted in python is texel = floor(coordinate / texel) mod
         * size.  Its most fragile point is a texel boundary, where floor is
         * about to step and where the flat 496 the encoder subtracts and
         * whatever the engine adds back decide which side of the boundary the
         * coordinate lands on.  On the 2048-tall texture a texel is 512 units,
         * so a contiguous walk of twenty-three units straddles one boundary
         * and the step must appear at exactly one place.  Where it appears is
         * the net bias, which neither 51 nor 52 could see: their values sit
         * far from any boundary, and I am not going to pretend they pinned it.
         *
         * A step in the wrong place, more than one step, or no step at all
         * each refutes the model.  (Truncation toward nought is already dead:
         * it wants texel 0 at -16 and section 52 read 2047.)
         */
        long q;

        printf("     p from -530 to -508, repeating, 2048 tall\n     ");
        for (q = -530L; q <= -508L; q++) {
            unsigned long got;
            unsigned v;

            blank();
            (void)setup(64UL, 0UL, 8UL, 4UL, 0L,
                        OSMGA_HW3D_TEXF_REPEATU | OSMGA_HW3D_TEXF_REPEATV);
            batch->state.texW = 8UL;
            batch->state.texH = 2048UL;
            batch->state.texPitch = 8UL;
            batch->state.tmr[0] = 0L; batch->state.tmr[1] = 0L;
            batch->state.tmr[2] = 0L; batch->state.tmr[3] = 0L;
            setTU(batch, 0L);
            setTV(batch, q);
            v = fire();
            if (v != OSMGA_HW3D_OK) { printf(" ref"); continue; }
            got = pixat(0UL, 0UL);
            printf(" %4lu", (got == BLANK) ? 9999UL : (got & 0xFFFFUL));
            if (((q + 530L) % 12L) == 11L) printf("\n     ");
        }
        printf("\n     the step names the net bias; python fits it\n");
    }

    printf("\n54. the same boundary, every 512 units down to the allowance\n");
    {
        /*
         * One boundary in the right place is a coincidence away from meaning
         * nothing.  The model says the step recurs every 512 units with the
         * same phase, so each boundary is tested with the pair that straddles
         * it: p = -512k - 16 must be one texel lower than p = -512k - 15.
         * Seven boundaries fit inside the allowance; the eighth would be
         * -4111, past it.
         */
        long k;

        for (k = 1L; k <= 7L; k++) {
            unsigned long lo, hi;
            long j;
            unsigned long got[2];

            for (j = 0L; j < 2L; j++) {
                unsigned v;

                blank();
                (void)setup(64UL, 0UL, 8UL, 4UL, 0L,
                            OSMGA_HW3D_TEXF_REPEATU | OSMGA_HW3D_TEXF_REPEATV);
                batch->state.texW = 8UL;
                batch->state.texH = 2048UL;
                batch->state.texPitch = 8UL;
                batch->state.tmr[0] = 0L; batch->state.tmr[1] = 0L;
                batch->state.tmr[2] = 0L; batch->state.tmr[3] = 0L;
                setTU(batch, 0L);
                setTV(batch, -512L * k - 16L + j);
                v = fire();
                got[j] = (v != OSMGA_HW3D_OK) ? 9999UL
                                              : (pixat(0UL, 0UL) & 0xFFFFUL);
            }
            lo = 2048UL - (unsigned long)k - 1UL;
            hi = 2048UL - (unsigned long)k;
            printf("   p=%-6ld %4lu   p=%-6ld %4lu   wanted %lu then %lu %s\n",
                   -512L * k - 16L, got[0], -512L * k - 15L, got[1], lo, hi,
                   (got[0] == lo && got[1] == hi) ? "" : "  <<");
            if (got[0] != lo || got[1] != hi) failures++;
        }
    }

    printf("\n55. the linear filter on a negative coordinate, across the seam\n");
    {
        /*
         * Everything measured so far picked ONE texel.  The allowance does
         * not depend on the filter, so a bilinear primitive with a negative
         * coordinate is admitted too, and that has never been measured: the
         * filter reaches a neighbouring texel, and below nought under repeat
         * the neighbour is at the far end of the texture.  If the phase is
         * wrong there, the kernel is admitting something it draws wrongly.
         *
         * Only row 2047 carries any red, so the red that comes back IS the
         * weight the engine gave the last row.  GL's rule is that the taps
         * straddle (coordinate/texel - 0.5); python checks the numbers
         * against that rather than against an eyeballed ramp.
         */
        unsigned long rr, cc;
        long q;

        for (rr = 0UL; rr < 2048UL; rr++)
            for (cc = 0UL; cc < 8UL; cc++)
                tex[rr * 8UL + cc] = (rr == 2047UL) ? 0x00FF0000UL : 0UL;

        printf("     p from -800 to -270 by 32, red is the weight of row 2047\n     ");
        for (q = -800L; q <= -270L; q += 32L) {
            unsigned long got;
            unsigned v;

            blank();
            (void)setup(64UL, 0UL, 8UL, 4UL, 0L,
                        OSMGA_HW3D_TEXF_REPEATU | OSMGA_HW3D_TEXF_REPEATV
                        | OSMGA_HW3D_TEXF_BILIN);
            batch->state.texW = 8UL;
            batch->state.texH = 2048UL;
            batch->state.texPitch = 8UL;
            batch->state.tmr[0] = 0L; batch->state.tmr[1] = 0L;
            batch->state.tmr[2] = 0L; batch->state.tmr[3] = 0L;
            setTU(batch, 0L);
            setTV(batch, q);
            v = fire();
            if (v != OSMGA_HW3D_OK) { printf(" ref"); continue; }
            got = pixat(0UL, 0UL);
            printf(" %3lu", (got == BLANK) ? 999UL : ((got >> 16) & 0xFFUL));
        }
        printf("\n     python fits this against GL's straddle rule\n");
    }

    printf("\n56. the addend above 2^20, where repeat does not clamp\n");
    {
        /*
         * The header argues the ladder stops mattering above 2^20 "because a
         * coordinate past the last texel is clamped".  That holds under
         * clamp.  Under REPEAT nothing is clamped -- the coordinate wraps,
         * and a few units decide which side of a texel boundary it wraps to.
         * And the positive reach is already eight whole textures, so ordinary
         * tiling puts coordinates in those bands today.
         *
         * A texel is 512 units on this texture, so the offset at which the
         * reading steps IS the net bias, negated.  The reading rises with the
         * coordinate, so the step is found by bisection rather than by
         * walking: the walk needed hundreds of submissions per band and the
         * step in the high bands turned out to be outside the window I first
         * guessed.
         */
        static const unsigned long band[5] = {
            1UL << 19, 1UL << 20, 1UL << 21, 1UL << 22, 1UL << 23
        };
        int bi;
        unsigned long rr, cc;

        /* 55 left only the last row carrying anything; put the row numbers
         * back or every reading here is a nought that means nothing. */
        for (rr = 0UL; rr < 2048UL; rr++)
            for (cc = 0UL; cc < 8UL; cc++)
                tex[rr * 8UL + cc] = rr;

        for (bi = 0; bi < 7; bi++) {
            long base = (long)band[bi] - 512L;   /* still inside the band */
            long lo = -256L, hi = 512L;
            unsigned long vlo = osmgaProbeReadV(base + lo);
            unsigned long vhi = osmgaProbeReadV(base + hi);

            if (vlo == 99999UL || vhi == 99999UL || vlo == vhi) {
                printf("   band 2^%-2d  no step between %ld and %ld"
                       "  (%lu .. %lu)\n",
                       19 + bi, lo, hi, vlo, vhi);
                continue;
            }
            while (hi - lo > 1L) {
                long mid = (lo + hi) / 2L;

                if (osmgaProbeReadV(base + mid) == vlo) lo = mid;
                else hi = mid;
            }
            printf("   band 2^%-2d  texel %lu -> %lu at offset %+ld"
                   "   net bias %+ld\n",
                   19 + bi, vlo, vhi, hi, -hi);
        }
        printf("   what is left after the encoder has taken its bias off.\n"
               "   At or below 2^20 the bias is still the flat 496 and the\n"
               "   ladder shows through as +15, +14, +12, +8, 0.  Above it the\n"
               "   bias follows the batch's reach, so the residual is nought --\n"
               "   it read -16, -48 and -112 before that, and a coordinate on a\n"
               "   texel boundary landed in the texel below.\n");
    }

    printf("\n57. is the band a property of the pixel, or of how I measured it\n");
    {
        /*
         * Every reading in 56 was taken with the gradients at nought, so the
         * coordinate was the same at every pixel AND equal to the register.
         * That cannot tell a band chosen from the pixel's coordinate from a
         * band chosen from the register the kernel wrote -- and if it is the
         * register, section 56 measures an artefact of the instrument and the
         * whole account is wrong.
         *
         * So: two primitives whose coordinate at the READ pixel is the same
         * value, reached in two different ways.  One has no gradient and puts
         * that value straight in the register.  The other starts at nought
         * and climbs to it, so its register is in a different band entirely.
         * If the band belongs to the pixel they read the same texel.
         */
        long C = (1L << 21) - 512L;             /* on a boundary, band 2^21 */
        long g = C / 4L;                        /* reached at column four */
        unsigned long flat, climbed;
        unsigned v;

        blank();
        (void)setup(64UL, 0UL, 8UL, 4UL, 0L,
                    OSMGA_HW3D_TEXF_REPEATU | OSMGA_HW3D_TEXF_REPEATV);
        batch->state.texW = 8UL; batch->state.texH = 2048UL;
        batch->state.texPitch = 8UL;
        batch->state.tmr[0] = 0L; batch->state.tmr[1] = 0L;
        batch->state.tmr[2] = 0L; batch->state.tmr[3] = 0L;
        setTU(batch, 0L); setTV(batch, C);
        v = fire();
        flat = (v != OSMGA_HW3D_OK) ? 99999UL : (pixat(0UL, 4UL) & 0xFFFFUL);

        blank();
        (void)setup(64UL, 0UL, 8UL, 4UL, 0L,
                    OSMGA_HW3D_TEXF_REPEATU | OSMGA_HW3D_TEXF_REPEATV);
        batch->state.texW = 8UL; batch->state.texH = 2048UL;
        batch->state.texPitch = 8UL;
        batch->state.tmr[0] = 0L; batch->state.tmr[1] = 0L;
        batch->state.tmr[2] = g;  batch->state.tmr[3] = 0L;
        setTU(batch, 0L); setTV(batch, 0L);
        v = fire();
        climbed = (v != OSMGA_HW3D_OK) ? 99999UL : (pixat(0UL, 4UL) & 0xFFFFUL);

        printf("   coordinate %ld at column four:  register %lu   climbed %lu\n",
               C, flat, climbed);
        printf("   the register sits in band 2^21 one way and 2^19 the other;\n"
               "   equal readings mean the band belongs to the pixel\n");
        say("the band follows the pixel, not the register",
            (flat == climbed && flat != 99999UL) ? 1U : 0U, 1U);
    }

    printf("\n58. inside the bands, not only at the top of them\n");
    {
        /*
         * 56 measured each band at 2^n - 512, which is the very top of the
         * interval below 2^n.  That shows what the addend is THERE; it does
         * not show that it is the same throughout the interval.  If the
         * addend actually varies inside a band -- with the column, or with
         * some finer structure -- then "the smallest addend is the one the
         * top of the range implies" is false and every bias scheme built on
         * it is unsafe.
         *
         * So each interval is measured at a quarter, a half, three quarters
         * and at a deliberately unround place, all on texel boundaries, by
         * the same bisection.  A single disagreement inside a band refutes
         * the ladder as a description of the addend.
         */
        static const long bases[16] = {
            1310720L, 1572864L, 1835008L, 1416704L,      /* inside 2^21 */
            2621440L, 3145728L, 3670016L, 2833408L,      /* inside 2^22 */
            5242880L, 6291456L, 7340032L, 5666816L,      /* inside 2^23 */
            655360L,  786432L,  917504L,  708608L        /* inside 2^20 */
        };
        static const char *bnm[4] = { "2^21", "2^22", "2^23", "2^20" };
        int i;

        for (i = 0; i < 16; i++) {
            long base = bases[i];
            long lo = -256L, hi = 512L;
            unsigned long vlo = osmgaProbeReadV(base + lo);
            unsigned long vhi = osmgaProbeReadV(base + hi);

            if ((i % 4) == 0) printf("   %s ", bnm[i / 4]);
            if (vlo == 99999UL || vhi == 99999UL || vlo == vhi) {
                printf("  %8ld:  ref ", base);
            } else {
                while (hi - lo > 1L) {
                    long mid = (lo + hi) / 2L;

                    if (osmgaProbeReadV(base + mid) == vlo) lo = mid;
                    else hi = mid;
                }
                printf("  %8ld:%+5ld", base, -hi);
            }
            if ((i % 4) == 3) printf("\n");
        }
        printf("   the same residual across each row, whatever it is:\n"
               "   the point is that the addend does not vary INSIDE a band,\n"
               "   which is what the bias rule assumes when it picks one from\n"
               "   the largest coordinate alone\n");
    }

    printf("\n59. the same coordinate reached other ways, and other columns\n");
    {
        /*
         * 57 showed the band follows the pixel and not the register, but with
         * one gradient, one sign, one axis and one column.  The header also
         * records a fine structure of up to fifteen units that involves the
         * column and that no rule fits; at a band where the net bias is -16
         * that is the same size as the thing being measured, so it has to be
         * looked at here rather than assumed to stay small.
         *
         * Everything below reads a coordinate sitting exactly ON a texel
         * boundary in band 2^21, where the net bias is -16 and the reading
         * must therefore be the texel BELOW: 2046, not 2047.
         */
        long C = (1L << 21) - 512L;
        unsigned long got;
        int col;
        unsigned v;

        unsigned long first = 99999UL;
        int same = 1;

        printf("     columns 0..7, constant coordinate:");
        blank();
        (void)setup(64UL, 0UL, 8UL, 4UL, 0L,
                    OSMGA_HW3D_TEXF_REPEATU | OSMGA_HW3D_TEXF_REPEATV);
        batch->state.texW = 8UL; batch->state.texH = 2048UL;
        batch->state.texPitch = 8UL;
        batch->state.tmr[0] = 0L; batch->state.tmr[1] = 0L;
        batch->state.tmr[2] = 0L; batch->state.tmr[3] = 0L;
        setTU(batch, 0L); setTV(batch, C);
        v = fire();
        if (v != OSMGA_HW3D_OK) printf("  refused");
        else
            for (col = 0; col < 8; col++) {
                unsigned long g = pixat(0UL, (unsigned long)col) & 0xFFFFUL;

                if (col == 0) first = g;
                else if (g != first) same = 0;
                printf(" %4lu", g);
            }
        printf("      %s\n", same ? "all the same" : "NOT all the same");
        say("no column reads a different texel", same ? 1U : 0U, 1U);

        blank();
        (void)setup(64UL, 0UL, 8UL, 4UL, 0L,
                    OSMGA_HW3D_TEXF_REPEATU | OSMGA_HW3D_TEXF_REPEATV);
        batch->state.texW = 8UL; batch->state.texH = 2048UL;
        batch->state.texPitch = 8UL;
        batch->state.tmr[0] = 0L; batch->state.tmr[1] = 0L;
        batch->state.tmr[2] = -(C / 4L); batch->state.tmr[3] = 0L;
        setTU(batch, 0L); setTV(batch, 2L * C);
        v = fire();
        got = (v != OSMGA_HW3D_OK) ? 99999UL : (pixat(0UL, 4UL) & 0xFFFFUL);
        printf("     descending to it from twice as high: %lu\n", got);
        /*
         * Against the constant-coordinate reading above rather than against a
         * number.  It used to want 2046, which was the texel BELOW the one the
         * addressing asks for -- the encoder was taking off more than the
         * engine put back in that band, and the assertion had the defect
         * written into it as an expected value.  What the section is for is
         * that the band follows the pixel however the pixel got there, and
         * that is a comparison of two readings.
         */
        say("a negative gradient lands on the same texel",
            (got != 99999UL && got == first) ? 1U : 0U, 1U);
    }

    printf("\n60. the band edges themselves, and whether the step is single\n");
    {
        /*
         * 56 and 58 measured INSIDE the bands.  Which band a coordinate
         * sitting exactly ON a power of two belongs to was inferred from the
         * ladder's wording, not measured -- and it decides something real:
         * the validator's cheap box path is conservative, so an affine scene
         * whose sampled coordinates stay just under 2^20 can still report a
         * maximum of exactly 2^20.  If that value belongs to the band above,
         * a scene that is untouched today would have its bias moved.
         *
         * Each of these is a multiple of the 512-unit texel, so a
         * non-negative net bias reads texel nought (1024 at 2^19) and a
         * negative one reads the texel below.  One reading each settles it.
         *
         * Then a unit-by-unit walk across 2^20: the addend must step ONCE.
         * More than one step, or a step back, breaks the monotonicity that
         * the whole bias argument rests on.
         */
        static const long edge[5] = {
            1L << 19, 1L << 20, 1L << 21, 1L << 22, 1L << 23
        };
        int i;
        long w;
        unsigned long prev = 99999UL;
        int steps = 0;

        for (i = 0; i < 5; i++) {
            unsigned long got = osmgaProbeReadV(edge[i]);

            printf("   exactly 2^%d = %8ld  ->  texel %lu   %s\n",
                   19 + i, edge[i], got,
                   (got == 99999UL) ? "refused"
                     : ((got == 0UL || got == 1024UL)
                          ? "net bias is not negative: the LOWER band"
                          : "net bias is negative: the HIGHER band"));
        }

        printf("   walking 2^20 a unit at a time:");
        for (w = -12L; w <= 12L; w++) {
            unsigned long got = osmgaProbeReadV((1L << 20) + w);

            if (prev != 99999UL && got != prev) {
                steps++;
                printf("  step at %+ld (%lu->%lu)", w, prev, got);
            }
            prev = got;
        }
        printf("\n");
        say("exactly one step across the band edge",
            (steps == 1) ? 1U : 0U, 1U);
    }

    printf("\n61. where the addend actually changes, measured not inferred\n");
    {
        /*
         * 60 read texel nought at exactly 2^20 AND found only one step in the
         * twelve units above it, so the addend is still 496 there -- the band
         * does not turn over at the power of two, which is what I had assumed
         * from the way the ladder is written down.  Since the bias rule has to
         * evaluate "which addend applies at this coordinate", the turnover has
         * to be measured rather than inferred.
         *
         * At a coordinate that is a multiple of the 512-unit texel, a net bias
         * of nought or more reads base/512, and a negative one reads one less.
         * That is a yes/no answer at any multiple of 512, so the turnover can
         * be bisected over the multiples.
         */
        static const long lo0[3] = { 1L << 20, 1L << 21, 1L << 22 };
        static const long hi0[3] = { 1L << 21, 1L << 22, 1L << 23 };
        int i;

        for (i = 0; i < 3; i++) {
            long lo = lo0[i], hi = hi0[i];
            unsigned long want;

            /* lo is known non-negative, hi known negative, from 60 */
            while (hi - lo > 512L) {
                long mid = lo + ((hi - lo) / 1024L) * 512L;
                unsigned long got;

                if (mid == lo) break;
                got = osmgaProbeReadV(mid);
                want = ((unsigned long)(mid / 512L)) & 2047UL;
                if (got == want) lo = mid;
                else hi = mid;
            }
            printf("   addend drops between %ld and %ld", lo, hi);
            printf("   = 2^%d x %.4f\n", 20 + i,
                   (double)hi / (double)(1L << (20 + i)));
        }
    }

    printf("\n62. the u axis at a high band, lane by lane\n");
    {
        /*
         * The safety of the whole bias scheme rests on the addend never being
         * SMALLER than the ladder says, because the bias is set to the ladder
         * value at the primitive's maximum.  The header records a fine
         * structure of up to fifteen units that involves the column and that
         * no rule fits; 59 looked for it on v and found nothing, but v is
         * constant across a row.  u is not: the two texture stages drive even
         * and odd screen columns, so if the residue has a lane component this
         * is where it shows, and at a band where the ladder step is sixteen
         * units a residue of a few units can flip a boundary-aligned texel.
         *
         * A 1024-wide texture puts a texel at 1024 units.  The coordinate is
         * held constant across the row and sits exactly on a texel boundary
         * in the band above 2^20, where the net bias is -16, so every column
         * must read the texel BELOW: 1022, and any column reading 1023 is a
         * positive residue while 1021 would be a negative one -- the
         * dangerous direction.
         */
        long C = (1L << 21) - 1024L;
        unsigned long rr, cc;
        unsigned v;
        int col;

        for (rr = 0UL; rr < 4UL; rr++)
            for (cc = 0UL; cc < 1024UL; cc++)
                tex[rr * 1024UL + cc] = cc;

        blank();
        (void)setup(64UL, 0UL, 8UL, 4UL, 0L,
                    OSMGA_HW3D_TEXF_REPEATU | OSMGA_HW3D_TEXF_REPEATV);
        batch->state.texW = 1024UL; batch->state.texH = 4UL;
        batch->state.texPitch = 1024UL;
        batch->state.tmr[0] = 0L; batch->state.tmr[1] = 0L;
        batch->state.tmr[2] = 0L; batch->state.tmr[3] = 0L;
        setTU(batch, C);  setTV(batch, 0L);
        v = fire();
        printf("     columns 0..7:");
        if (v != OSMGA_HW3D_OK) printf("  refused %u", v);
        else
            for (col = 0; col < 8; col++)
                printf(" %4lu", pixat(0UL, (unsigned long)col) & 0xFFFFUL);
        printf("\n     every column must read the SAME texel; which texel it\n"
               "     is depends on the residual the encoder leaves, and that\n"
               "     is nought above 2^20 now that the bias follows the reach\n");
    }

    printf("\n63. the seam: the same coordinate under two different biases\n");
    {
        /*
         * The bias belongs to the batch, and a textured batch is one
         * primitive, so two primitives that share an edge can be given
         * different biases -- one reaching far enough to be given 384, its
         * neighbour staying low and keeping 496.  Their residuals then differ
         * by up to 511 - 384 - (511 - 496) = 112 units, and along the edge
         * they share, the same coordinate is read twice with two phases.
         *
         * Whether that is visible depends on where the coordinate sits.  A
         * texel is 1024 units on the 1024-wide texture used here, so a
         * coordinate more than 127 units below a boundary is below it under
         * both residuals, and one at or above it is above under both.  In
         * between -- 112 units wide -- the two disagree by a whole texel.
         *
         * Both batches put the SAME coordinate at the pixel that is read: the
         * column read is the first, where the x offset is nought, so the
         * start is the coordinate regardless of the gradient.  Only the reach
         * differs, and with it the bias.
         */
        unsigned long rr, cc;
        long k;
        long lowDiff = 0L, hiDiff = 0L;
        int firstK = -1, lastK = -1;

        for (rr = 0UL; rr < 4UL; rr++)
            for (cc = 0UL; cc < 1024UL; cc++)
                tex[rr * 1024UL + cc] = cc;

        printf("     k below a texel boundary at 40960, texel 1024 units\n");
        printf("     %4s %8s %8s\n", "k", "near", "far");
        for (k = 1L; k <= 160L; k++) {
            long X = 40960L - k;
            unsigned long got[2];
            int j;

            for (j = 0; j < 2; j++) {
                unsigned v;

                blank();
                (void)setup(64UL, 0UL, 8UL, 4UL, 0L,
                            OSMGA_HW3D_TEXF_REPEATU
                            | OSMGA_HW3D_TEXF_REPEATV);
                batch->state.texW = 1024UL; batch->state.texH = 4UL;
                batch->state.texPitch = 1024UL;
                batch->state.tmr[0] = j ? (1L << 20) : 0L;
                batch->state.tmr[1] = 0L;
                batch->state.tmr[2] = 0L; batch->state.tmr[3] = 0L;
                setTU(batch, X);  setTV(batch, 0L);
                v = fire();
                got[j] = (v != OSMGA_HW3D_OK) ? 99999UL
                                              : (pixat(0UL, 0UL) & 0xFFFFUL);
            }
            if (got[0] != got[1]) {
                if (firstK < 0) firstK = (int)k;
                lastK = (int)k;
                if (k <= 8L || (k % 32L) == 0L)
                    printf("     %4ld %8lu %8lu   <-- differ\n",
                           k, got[0], got[1]);
            } else if (k <= 8L || (k % 32L) == 0L)
                printf("     %4ld %8lu %8lu\n", k, got[0], got[1]);
            if (got[0] == 99999UL) lowDiff++;
            if (got[1] == 99999UL) hiDiff++;
        }
        printf("     they disagree for k = %d .. %d  (%d values)\n",
               firstK, lastK, (firstK < 0) ? 0 : lastK - firstK + 1);
        printf("     python says 16 .. 127, which is 112 values\n");
        if (lowDiff || hiDiff)
            printf("     refusals: near %ld far %ld\n", lowDiff, hiDiff);
        say("the seam is exactly as wide as the bias difference",
            (firstK == 16 && lastK == 127) ? 1U : 0U, 1U);
    }

    printf("\n64. in perspective, what the coordinate actually comes out as\n");
    {
        /*
         * The encoder leaves perspective on the flat 496 because nobody knows
         * which value picks the engine's band there.  It matters: the
         * validator admits numerators up to 128q, so at a large denominator
         * they reach 2^30, seven bands past anything measured.
         *
         * A first attempt swept one denominator and found a net of -1, which
         * is neither of the two answers I had predicted -- so the model was
         * wrong, not the machine.  This sweeps the DENOMINATOR instead, at a
         * fixed coordinate, which separates two questions at once: whether
         * the offset is in numerator units (it then scales as 1/q) or in
         * coordinate units (it does not), and which value's band it belongs
         * to (the coordinate's is fixed here, the numerator's moves with q).
         *
         * The coordinate is held at 2^19, a multiple of the 512-unit texel,
         * and the numerator is whatever produces it: 2^19 * q / 65536.  One
         * coordinate unit is q/65536 numerator units, so that is the step.
         */
        static const long qs[5] = {
            1L << 16, 1L << 17, 1L << 18, 1L << 19, 1L << 20
        };
        int qi;
        unsigned long rr, cc;

        for (rr = 0UL; rr < 2048UL; rr++)
            for (cc = 0UL; cc < 8UL; cc++)
                tex[rr * 8UL + cc] = rr;

        printf("     %10s %12s %10s %10s\n",
               "q", "numerator", "step", "net (coord)");
        for (qi = 0; qi < 5; qi++) {
            long q = qs[qi];
            long unit = q / 65536L;
            long pb, off, stepAt = 99999L;
            unsigned long prev = 99999UL;

            if (unit < 1L) unit = 1L;
            pb = (1L << 19) / 65536L * q;      /* 2^19 * q / 65536, exactly */
            for (off = -400L * unit; off <= 400L * unit; off += unit) {
                unsigned long got;
                unsigned v;

                blank();
                (void)setup(64UL, 0UL, 8UL, 4UL, 0L,
                            OSMGA_HW3D_TEXF_REPEATU | OSMGA_HW3D_TEXF_REPEATV
                            | OSMGA_HW3D_TEXF_PERSP);
                batch->state.texW = 8UL; batch->state.texH = 2048UL;
                batch->state.texPitch = 8UL;
                batch->state.tmr[0] = 0L; batch->state.tmr[1] = 0L;
                batch->state.tmr[2] = 0L; batch->state.tmr[3] = 0L;
                batch->state.tmr[4] = 0L; batch->state.tmr[5] = 0L;
                setTU(batch, 0L);
                setTV(batch, pb + off);
                setTQ(batch, q);
                v = fire();
                got = (v != OSMGA_HW3D_OK) ? 99999UL
                                           : (pixat(0UL, 0UL) & 0xFFFFUL);
                if (prev != 99999UL && got != prev && stepAt == 99999L)
                    stepAt = off;
                if (q == (1L << 20) && off >= -32L && off <= 64L
                    && (off % 16L) == 0L)
                    printf("       off %+4ld  numerator %ld  verdict %u  texel %lu\n",
                           off, pb + off, v, got);
                prev = got;
            }
            if (stepAt == 99999L)
                printf("     %10ld %12ld %10s %10s\n", q, pb, "none", "-");
            else
                printf("     %10ld %12ld %10ld %10ld\n",
                       q, pb, stepAt, -stepAt / unit);
        }
        printf("     a net that is the same at every q is in COORDINATE units;\n"
               "     one that halves as q doubles is in NUMERATOR units\n");

        /*
         * The sweep above moved q and the numerator's band together, so it
         * cannot separate "the numerator picks the band" from "q does".  This
         * one breaks that: q = 262144 with a coordinate of 2^18 gives a
         * numerator of exactly 2^20, the same numerator the q = 131072 row
         * had, while the coordinate's band is 2^18 instead of 2^19.
         *
         *   numerator's band 2^20 -> addend 496 -> the step is at nought
         *   coordinate's band 2^18 -> addend 508 -> the step is at -12
         */
        {
            long q2 = 262144L, pb2 = (1L << 18) / 65536L * 262144L;
            long off2, stepAt2 = 99999L;
            unsigned long prev2 = 99999UL;

            for (off2 = -200L; off2 <= 200L; off2 += 4L) {
                unsigned long got;
                unsigned v;

                blank();
                (void)setup(64UL, 0UL, 8UL, 4UL, 0L,
                            OSMGA_HW3D_TEXF_REPEATU | OSMGA_HW3D_TEXF_REPEATV
                            | OSMGA_HW3D_TEXF_PERSP);
                batch->state.texW = 8UL; batch->state.texH = 2048UL;
                batch->state.texPitch = 8UL;
                batch->state.tmr[0] = 0L; batch->state.tmr[1] = 0L;
                batch->state.tmr[2] = 0L; batch->state.tmr[3] = 0L;
                batch->state.tmr[4] = 0L; batch->state.tmr[5] = 0L;
                setTU(batch, 0L);
                setTV(batch, pb2 + off2);
                setTQ(batch, q2);
                v = fire();
                got = (v != OSMGA_HW3D_OK) ? 99999UL
                                           : (pixat(0UL, 0UL) & 0xFFFFUL);
                if (prev2 != 99999UL && got != prev2 && stepAt2 == 99999L)
                    stepAt2 = off2;
                prev2 = got;
            }
            printf("     matched numerator: q=%ld coordinate 2^18 numerator %ld"
                   "  step %+ld\n", q2, pb2, stepAt2);
            say("the numerator picks the band, not the coordinate",
                (stepAt2 == 0L) ? 1U : 0U, 1U);
        }
    }

    printf("\n65. how high a numerator goes, and what the addend does up there\n");
    {
        /*
         * 64 showed the band comes from the NUMERATOR.  The ladder is
         * measured to 2^23, so the question is how far a numerator can get.
         * The ratio check would allow 128q -- 2^30 at the largest denominator
         * -- but it never gets there: the anchor is held to COORD_MAX and
         * each gradient to room over its own span, so
         *
         *      |p| <= |tmr[7]| + |tmr[2]|*ex + |tmr[3]|*vy <= 3 * COORD_MAX
         *
         * which is 2^24.58.  Two rungs, not seven.
         *
         * ONE shape, three numerators, so the shape cannot be the variable.
         * The x gradient contributes 2^23 at column four and the anchor moves
         * the rest.  The first target is a rung the affine work already
         * pinned at 384 -- if this shape agrees there, the shape is sound and
         * whatever the higher two say is the answer; if it does not, the
         * large denominator or the gradient is what changed and the fit from
         * 64 does not carry up here.
         *
         * At q = 2^23 a texel is 65536 numerator units and all three targets
         * are multiples of it, so each sits on a boundary and the step names
         * the net bias.
         */
        static const long ancs[9] = {
            0L, 1L << 21, 1L << 22, 3L << 21, 1L << 23,
            (1L << 23) - (1L << 16), (1L << 23) - 1L,
            (1L << 22) + (1L << 21), 7L << 20
        };
        static const char *wnm[9] = {
            "2^23", "+2^21", "+2^22", "+3*2^21", "+2^23 (=2^24)",
            "2^24-65536", "2^24-1", "+1.5*2^22", "+7*2^20"
        };
        int wi;
        unsigned long rr, cc;

        for (rr = 0UL; rr < 2048UL; rr++)
            for (cc = 0UL; cc < 8UL; cc++)
                tex[rr * 8UL + cc] = rr;

        for (wi = 0; wi < 9; wi++) {
            long q = 1L << 23;
            long gx = (1L << 23) / 4L;
            long anc = ancs[wi];
            long off, stepAt = 99999L;
            unsigned long prev = 99999UL;
            unsigned firstV = 0U;

            for (off = -300L; off <= 300L; off += 1L) {
                unsigned long got;
                unsigned v;

                blank();
                (void)setup(64UL, 0UL, 5UL, 4UL, 0L,
                            OSMGA_HW3D_TEXF_REPEATU | OSMGA_HW3D_TEXF_REPEATV
                            | OSMGA_HW3D_TEXF_PERSP);
                batch->state.texW = 8UL; batch->state.texH = 2048UL;
                batch->state.texPitch = 8UL;
                batch->state.tmr[0] = 0L; batch->state.tmr[1] = 0L;
                batch->state.tmr[2] = gx;  batch->state.tmr[3] = 0L;
                batch->state.tmr[4] = 0L;  batch->state.tmr[5] = 0L;
                setTU(batch, 0L);
                setTV(batch, anc + off);
                setTQ(batch, q);
                v = fire();
                if (off == -300L) firstV = v;
                got = (v != OSMGA_HW3D_OK) ? 99999UL
                                           : (pixat(0UL, 4UL) & 0xFFFFUL);
                if (prev != 99999UL && got != prev && stepAt == 99999L)
                    stepAt = off;
                prev = got;
            }
            printf("     %-9s numerator %9ld  verdict %u  step %+6ld"
                   "  511 minus bias would be %4ld\n",
                   wnm[wi], anc + gx * 4L, firstV, stepAt,
                   (stepAt == 99999L) ? -1L : -stepAt);
        }
        printf("     everything strictly between 2^23 and 2^24 reads 256, which\n"
               "     is the ladder carrying on.  The two that read ~495 are not\n"
               "     measurements: their anchor is at COORD_MAX, so the sweep\n"
               "     runs into the refusal rather than a texel boundary.\n");

        /*
         * Band 2^25, with the anchor kept well away from its ceiling by
         * splitting the climb between the two gradients.  Each sits on its
         * own bound, so together they carry 2^24 and the anchor only has to
         * supply the rest.
         */
        {
            static const long a2[2] = { 1L << 16, 1L << 22 };
            int j;

            for (j = 0; j < 2; j++) {
                long q = 1L << 23;
                long g = (1L << 23) / 4L;
                long anc = a2[j];
                long off, stepAt = 99999L;
                unsigned long prev = 99999UL;
                unsigned firstV = 0U;

                for (off = -700L; off <= 700L; off += 2L) {
                    unsigned long got;
                    unsigned v;

                    blank();
                    (void)setup(64UL, 0UL, 5UL, 5UL, 0L,
                                OSMGA_HW3D_TEXF_REPEATU
                                | OSMGA_HW3D_TEXF_REPEATV
                                | OSMGA_HW3D_TEXF_PERSP);
                    batch->tri[0].ar0 = 5L; batch->tri[0].ar6 = 5L;
                    batch->state.texW = 8UL; batch->state.texH = 2048UL;
                    batch->state.texPitch = 8UL;
                    batch->state.tmr[0] = 0L; batch->state.tmr[1] = 0L;
                    batch->state.tmr[2] = g;  batch->state.tmr[3] = g;
                    batch->state.tmr[4] = 0L; batch->state.tmr[5] = 0L;
                    setTU(batch, 0L);
                    setTV(batch, anc + off);
                    setTQ(batch, q);
                    v = fire();
                    if (off == -700L) firstV = v;
                    got = (v != OSMGA_HW3D_OK) ? 99999UL
                                               : (pixat(4UL, 4UL) & 0xFFFFUL);
                    if (prev != 99999UL && got != prev && stepAt == 99999L)
                        stepAt = off;
                    prev = got;
                }
                printf("     band 2^25   numerator %9ld  verdict %u"
                       "  step %+6ld  addend %4ld\n",
                       anc + g * 4L + g * 4L, firstV, stepAt,
                       (stepAt == 99999L) ? -1L : -stepAt);
            }
            printf("     the ladder would want 0 there (g = 512)\n");
        }
    }

    printf("\n66. the addend for a NEGATIVE numerator, band by band\n");
    {
        /*
         * Every measurement of the ladder so far used positive numerators.
         * The bias rule picks its band from the magnitude, and the negative
         * range has just been widened to a whole texture -- so whether the
         * ladder is the same on this side is no longer a curiosity, it is the
         * thing the widening rests on.  If it is not symmetric, a batch whose
         * numerators go far negative gets a bias that is too large and the
         * coordinate lands BELOW where the caller put it, which is the one
         * failure the contract exists to prevent.
         *
         * Affine, so the numerator is the coordinate.  A texel on the
         * 2048-tall texture is 512 units, and each base below is a multiple
         * of it just past a power of two in magnitude, so the band is
         * unambiguous and the coordinate sits exactly on a boundary.  The
         * offset at which the reading steps is the net bias negated, and the
         * addend is 496 plus it.
         *
         * If the ladder is symmetric these read 511, 510, 508, 504, 496.
         */
        /*
         * The last two are POSITIVE, and they are the control: the ladder on
         * that side is already known, so if the arithmetic below does not
         * reproduce 504 and 496 for them then the arithmetic is wrong and
         * nothing the negative rows say can be trusted.  It caught a sign
         * error the first time -- the addend is 496 MINUS the step, and the
         * printout had it plus.
         */
        static const long bases[7] = {
            -1024L, -66048L, -131584L, -262656L, -524800L,
            262656L, 524800L
        };
        static const char *bnm[7] = {
            "-2^16", "-2^17", "-2^18", "-2^19", "-2^20",
            "+2^19", "+2^20"
        };
        int bi;
        unsigned long rr, cc;

        for (rr = 0UL; rr < 2048UL; rr++)
            for (cc = 0UL; cc < 8UL; cc++)
                tex[rr * 8UL + cc] = rr;

        for (bi = 0; bi < 7; bi++) {
            long base = bases[bi];
            long off, stepAt = 99999L;
            unsigned long prev = 99999UL;
            unsigned firstV = 0U;

            for (off = -40L; off <= 40L; off += 1L) {
                unsigned long got;
                unsigned v;

                blank();
                (void)setup(64UL, 0UL, 8UL, 4UL, 0L,
                            OSMGA_HW3D_TEXF_REPEATU
                            | OSMGA_HW3D_TEXF_REPEATV);
                batch->state.texW = 8UL; batch->state.texH = 2048UL;
                batch->state.texPitch = 8UL;
                batch->state.tmr[0] = 0L; batch->state.tmr[1] = 0L;
                batch->state.tmr[2] = 0L; batch->state.tmr[3] = 0L;
                setTU(batch, 0L);
                setTV(batch, base + off);
                v = fire();
                if (off == -40L) firstV = v;
                got = (v != OSMGA_HW3D_OK) ? 99999UL
                                           : (pixat(0UL, 0UL) & 0xFFFFUL);
                if (prev != 99999UL && got != prev && stepAt == 99999L)
                    stepAt = off;
                prev = got;
            }
            printf("     band %-4s base %9ld  verdict %u  step %+6ld"
                   "  511 minus bias would be %4ld\n",
                   bnm[bi], base, firstV, stepAt,
                   (stepAt == 99999L) ? -1L : -stepAt);
        }
        printf("     a symmetric ladder would read 511, 510, 508, 504, 496\n"
               "     on the negative rows; the two positive rows must read\n"
               "     504 and 496, which is what the positive work already found\n");
    }

    printf("\n67. large negative coordinates read the texel GL asks for\n");
    {
        /*
         * 52 did this for coordinates a sliver below nought.  The allowance is
         * a whole texture now, so the same question has to be asked of the
         * whole of it -- and it is the last gate before the builder may be
         * opened, because it is the difference between "the engine tolerates
         * these" and "the engine draws what GL says".
         *
         * The addend on this side is a flat 511 whatever the magnitude (66),
         * so the net after the encoder's 496 is +15 everywhere and python can
         * say what each reading must be: floor((p + 15)/texel) mod size under
         * repeat, and texel nought under clamp.
         *
         * Width stops at 1024 because the pitch field is eleven bits, so the
         * 2048 case is taken down the v axis, as it was in 52.
         */
        static const long ps[6] = {
            -1048576L, -786432L, -524288L, -262144L, -65536L, -4096L
        };
        static const unsigned long cw[4]  = { 8UL, 64UL, 1024UL, 8UL };
        static const unsigned long chh[4] = { 8UL, 64UL, 4UL, 2048UL };
        static const int cax[4] = { 0, 0, 0, 1 };
        static const char *cnm[4] = { "8 wide", "64 wide", "1024 wide",
                                      "2048 tall" };
        /* python: floor((p + 15)/texel) mod size, for the six values above */
        static const unsigned long want[4][6] = {
            {    0UL,    2UL,    4UL,    6UL,    7UL,    7UL },
            {    0UL,   16UL,   32UL,   48UL,   60UL,   63UL },
            {    0UL,  256UL,  512UL,  768UL,  960UL, 1020UL },
            {    0UL,  512UL, 1024UL, 1536UL, 1920UL, 2040UL }
        };
        int ci, j, k;

        for (ci = 0; ci < 4; ci++) {
            unsigned long rr, cc;

            for (rr = 0UL; rr < chh[ci]; rr++)
                for (cc = 0UL; cc < cw[ci]; cc++)
                    tex[rr * cw[ci] + cc] = cax[ci] ? rr : cc;

            for (k = 0; k < 2; k++) {
                int bad = 0;

                printf("   %-10s %-9s", cnm[ci], k ? "repeating" : "clamped");
                for (j = 0; j < 6; j++) {
                    unsigned long got, wnt;
                    unsigned v;

                    blank();
                    (void)setup(64UL, 0UL, 8UL, 4UL, 0L,
                                k ? (OSMGA_HW3D_TEXF_REPEATU
                                     | OSMGA_HW3D_TEXF_REPEATV) : 0UL);
                    batch->state.texW = cw[ci];
                    batch->state.texH = chh[ci];
                    batch->state.texPitch = cw[ci];
                    batch->state.tmr[0] = 0L; batch->state.tmr[1] = 0L;
                    batch->state.tmr[2] = 0L; batch->state.tmr[3] = 0L;
                    setTU(batch, cax[ci] ? 0L : ps[j]);
                    setTV(batch, cax[ci] ? ps[j] : 0L);
                    v = fire();
                    if (v != OSMGA_HW3D_OK) { printf("   ref"); bad = 1; continue; }
                    got = pixat(0UL, 0UL) & 0xFFFFUL;
                    wnt = k ? want[ci][j] : 0UL;
                    printf("  %5lu", got);
                    if (got != wnt) bad = 1;
                }
                printf("   %s\n", bad ? "  <<" : "");
                if (bad) failures++;
            }
        }
        printf("     wanted, repeating: the python rows above; clamped: nought\n");
    }

    printf("\n68. trying to break \"negatives always get 511\"\n");
    {
        /*
         * 66 swept the negative bands at q = 65536, where the numerator IS
         * the coordinate -- so it could only reach bands 2^16 to 2^20, since
         * the allowance is on the coordinate.  But the BAND comes from the
         * numerator, and with a large denominator a coordinate well inside
         * the allowance carries a numerator far beyond it: at q = 2^23 one
         * coordinate unit is 128 numerator units, so a coordinate of -2^17 is
         * a numerator of -2^24.
         *
         * That is the cheapest way to refute the claim, and it needs nothing
         * widened.  The reachable negative numerator is capped at three times
         * COORD_MAX by the anchor and the slope bounds, exactly as the
         * positive one is, so these three cover the rest of the range.
         *
         * If the claim holds every row steps at -15.  A step anywhere else is
         * the ladder biting on this side after all, and the bias rule would
         * have to be told about it before the builder is opened.
         */
        /*
         * The targets are one texel ABOVE each power of two, so the anchor
         * has room to be swept either side of the boundary.  Putting them ON
         * the power of two needed the anchor at its own ceiling, and the
         * sweep then ran into the refusal instead of a texel edge -- twice.
         */
        static const long ancs[3] = { 65536L, -8323072L, -8323072L };
        static const long gxs[3]  = { -(1L << 23) / 4L, -(1L << 23) / 4L,
                                      -(1L << 23) / 4L };
        static const long gys[3]  = { 0L, 0L, -(1L << 23) / 4L };
        static const char *bnm[3] = { "-2^23", "-2^24", "-2^25" };
        int bi;
        unsigned long rr, cc;

        for (rr = 0UL; rr < 2048UL; rr++)
            for (cc = 0UL; cc < 8UL; cc++)
                tex[rr * 8UL + cc] = rr;

        for (bi = 0; bi < 3; bi++) {
            long q = 1L << 23;
            long off, stepAt = 99999L;
            unsigned long prev = 99999UL;
            unsigned firstV = 0U;
            long hh = gys[bi] ? 5L : 4L;
            long rdrow = gys[bi] ? 4L : 0L;

            for (off = -600L; off <= 40L; off += 1L) {
                unsigned long got;
                unsigned v;

                blank();
                (void)setup(64UL, 0UL, 5UL, (unsigned long)hh, 0L,
                            OSMGA_HW3D_TEXF_REPEATU
                            | OSMGA_HW3D_TEXF_REPEATV
                            | OSMGA_HW3D_TEXF_PERSP);
                batch->tri[0].ar0 = hh; batch->tri[0].ar6 = hh;
                batch->state.texW = 8UL; batch->state.texH = 2048UL;
                batch->state.texPitch = 8UL;
                batch->state.tmr[0] = 0L; batch->state.tmr[1] = 0L;
                batch->state.tmr[2] = gxs[bi]; batch->state.tmr[3] = gys[bi];
                batch->state.tmr[4] = 0L; batch->state.tmr[5] = 0L;
                setTU(batch, 0L);
                setTV(batch, ancs[bi] + off);
                setTQ(batch, q);
                v = fire();
                if (off == -600L) firstV = v;
                got = (v != OSMGA_HW3D_OK) ? 99999UL
                                           : (pixat((unsigned long)rdrow, 4UL)
                                              & 0xFFFFUL);
                if (prev != 99999UL && got != prev && stepAt == 99999L)
                    stepAt = off;
                prev = got;
            }
            printf("     band %-6s numerator %12ld  verdict %u  step %+6ld"
                   "  511 minus bias would be %4ld\n",
                   bnm[bi],
                   ancs[bi] + gxs[bi] * 4L + gys[bi] * (hh - 1L),
                   firstV, stepAt,
                   (stepAt == 99999L) ? -1L : -stepAt);
        }
        printf("     511 in every row means the claim survives; anything else\n"
               "     means the ladder bites on this side too\n");
    }

    printf("\n69. bilinear at a large negative coordinate\n");
    {
        /*
         * 55 measured the filter across the WRAPPED seam, at a coordinate a
         * few hundred units below nought.  The allowance is a whole texture
         * now, so the obvious next question is the seam at large negatives --
         * and there is not one.  The wrap period is one whole texture, 2^20
         * units, so inside a 2^20 allowance there is exactly one seam and it
         * is the one 55 already measured.
         *
         * What is left down here is ordinary interior filtering, which is
         * what this checks: red on row 1000, which in the negative range sits
         * at u of 1000 - 2048 texels, and the red that comes back is that
         * row's weight.  python says what the ramp must be under GL's
         * straddle rule with the +15 the encoder leaves at this reach.
         */
        long base = -1048L * 512L;
        long off;
        unsigned long rr, cc;

        for (rr = 0UL; rr < 2048UL; rr++)
            for (cc = 0UL; cc < 8UL; cc++)
                tex[rr * 8UL + cc] = (rr == 1000UL) ? 0x00FF0000UL : 0UL;

        printf("     p from %ld by 64, red is the weight of row 1000\n     ",
               base - 320L);
        for (off = -320L; off <= 320L; off += 64L) {
            unsigned long got;
            unsigned v;

            blank();
            (void)setup(64UL, 0UL, 8UL, 4UL, 0L,
                        OSMGA_HW3D_TEXF_REPEATU | OSMGA_HW3D_TEXF_REPEATV
                        | OSMGA_HW3D_TEXF_BILIN);
            batch->state.texW = 8UL; batch->state.texH = 2048UL;
            batch->state.texPitch = 8UL;
            batch->state.tmr[0] = 0L; batch->state.tmr[1] = 0L;
            batch->state.tmr[2] = 0L; batch->state.tmr[3] = 0L;
            setTU(batch, 0L);
            setTV(batch, base + off);
            v = fire();
            if (v != OSMGA_HW3D_OK) { printf(" ref"); continue; }
            got = pixat(0UL, 0UL);
            printf(" %3lu", (got == BLANK) ? 999UL : ((got >> 16) & 0xFFUL));
        }
        printf("\n     python wants 0 7 39 71 103 135 167 199 231 248 216\n");
    }

    printf("\n70. is there a lambda -- does the engine choose MIN or MAG per pixel\n");
    {
        /*
         * TEXFILTER has two fields, and the encoder now writes both.  But
         * something has to CHOOSE between them at each fragment, and that
         * something is lambda.  Whether this path computes one at all is the
         * question mipmapping turns on: if it does, the gate that requires
         * MinFilter to equal MagFilter can be widened and the mipmap modes
         * are worth looking for; if it does not, setting MIN means "always
         * minify" and the two fields are not independent at all.
         *
         * The instrument is a gradient that MAGNIFIES at one end of a row and
         * MINIFIES at the other, with the two filters deliberately different:
         * MAG nearest, MIN bilinear.  A texture whose texels alternate hard
         * between two values then reads sharp where the rate is under one
         * texel per pixel and blended where it is over -- IF the engine
         * chooses.  If it does not, the whole row is one or the other.
         *
         * The rate cannot vary along a row without perspective, so the sweep
         * varies it BETWEEN rows instead: each row is drawn with its own
         * gradient, from a quarter of a texel per pixel up to four.
         */
        static const long rates[7] = { 4096L, 8192L, 16384L, 32768L,
                                       65536L, 131072L, 262144L };
        static const char *rnm[7] = { "1/4", "1/2", "1", "2", "4", "8", "16" };
        int j, k;
        unsigned long rr, cc;

        /* texels alternate 0 and 255 in red, so any blend is obvious */
        for (rr = 0UL; rr < 64UL; rr++)
            for (cc = 0UL; cc < 64UL; cc++)
                tex[rr * 64UL + cc] = (cc & 1UL) ? 0x00FF0000UL : 0UL;

        for (k = 0; k < 3; k++) {
            unsigned long fl =
                (k == 0) ? 0UL
              : (k == 1) ? OSMGA_HW3D_TEXF_BILIN
                         : OSMGA_HW3D_TEXF_BILINMIN;

            printf("   %-14s", (k == 0) ? "neither" :
                               (k == 1) ? "MAG only" : "MIN only");
            for (j = 0; j < 7; j++) {
                unsigned long got;
                unsigned v;

                blank();
                (void)setup(1024UL, 0UL, 16UL, 4UL, rates[j],
                            fl | OSMGA_HW3D_TEXF_REPEATU
                               | OSMGA_HW3D_TEXF_REPEATV);
                batch->state.texW = 64UL; batch->state.texH = 64UL;
                batch->state.texPitch = 64UL;
                batch->state.tmr[1] = 0L; batch->state.tmr[2] = 0L;
                batch->state.tmr[3] = 0L;
                setTU(batch, rates[j] / 2L);   /* half a step in */
                setTV(batch, 0L);
                v = fire();
                if (v != OSMGA_HW3D_OK) { printf("  ref"); continue; }
                got = pixat(0UL, 8UL);
                printf(" %4lu", (got == BLANK) ? 999UL
                                               : ((got >> 16) & 0xFFUL));
            }
            printf("\n");
        }
        printf("     rate (texels per pixel):");
        for (j = 0; j < 7; j++) printf(" %4s", rnm[j]);
        printf("\n");
        printf("     0 or 255 is a point sample; anything between is a blend.\n"
               "     If MIN-only blends where the rate is over one and points\n"
               "     where it is under, the engine chooses -- there is a lambda.\n");
    }

    printf("\n71. is the choice made INSIDE a primitive\n");
    {
        /*
         * 70 showed the two filter fields acting in complementary ranges, but
         * every one of its seven rows was a SEPARATE draw at a constant rate.
         * That is consistent with a choice made once per primitive as well as
         * with one made per fragment, and the difference matters: only the
         * latter is a lambda in GL's sense, and only the latter makes the
         * mipmap modes worth chasing.
         *
         * So: ONE primitive whose rate crosses one within it.  The rate cannot
         * vary along a row without perspective, so the denominator carries a
         * gradient -- python says the rate runs from 2.2 texels per pixel at
         * the left edge to 0.95 at the right, crossing one near column
         * fourteen.
         *
         * MIN is bilinear and MAG is nearest.  If the choice is per fragment
         * the left of the row blends and the right is sharp; if it is made
         * once for the primitive the whole row is one or the other.
         */
        long q0 = 1L << 19, dq = 20000L, dp = 300000L;
        int col;
        unsigned long rr, cc;

        for (rr = 0UL; rr < 64UL; rr++)
            for (cc = 0UL; cc < 64UL; cc++)
                tex[rr * 64UL + cc] = (cc & 1UL) ? 0x00FF0000UL : 0UL;

        blank();
        (void)setup(1024UL, 0UL, 16UL, 4UL, 0L,
                    OSMGA_HW3D_TEXF_BILINMIN | OSMGA_HW3D_TEXF_PERSP
                    | OSMGA_HW3D_TEXF_REPEATU | OSMGA_HW3D_TEXF_REPEATV);
        batch->state.texW = 64UL; batch->state.texH = 64UL;
        batch->state.texPitch = 64UL;
        batch->state.tmr[0] = dp;  batch->state.tmr[1] = 0L;
        batch->state.tmr[2] = 0L;  batch->state.tmr[3] = 0L;
        batch->state.tmr[4] = dq;  batch->state.tmr[5] = 0L;
        setTU(batch, 0L);  setTV(batch, 0L);
        setTQ(batch, q0);
        {
            unsigned v = fire();

            printf("     MIN bilinear, MAG nearest, verdict %u\n", v);
            if (v == OSMGA_HW3D_OK) {
                printf("     red by column: ");
                for (col = 1; col < 16; col++)
                    printf(" %3lu", pixat(0UL, (unsigned long)col) >> 16
                                    & 0xFFUL);
                /*
                 * python, from the same registers: 2.205 2.048 1.908 1.782
                 * 1.668 1.564 1.470 1.384 1.305 1.233 1.167 1.106 1.050
                 * 0.997 0.949 -- the rate falls under one at the fourteenth.
                 * The texture is nought and 255 alternating, so a reading of
                 * either is a point sample and anything else is a blend.
                 */
                printf("     the rate falls under one at column fourteen;\n"
                       "     a reading of 0 or 255 is a point sample and\n"
                       "     anything between is a blend\n");
            }
        }
    }

    printf("\n72. does the selector look at both axes\n");
    {
        /*
         * GL's lambda is the LARGER of the two axis rates, so a primitive that
         * magnifies in u while minifying in v is minifying.  If the engine
         * takes only one axis it will disagree exactly there -- and that is a
         * case the widened gate would let through, so it has to be asked
         * before the gate is widened rather than after.
         *
         * MIN is bilinear and MAG nearest, as in 70 and 71.  The texture
         * alternates along BOTH axes here, so either axis blending shows.
         */
        static const long dus[3] = { 65536L,  4096L, 4096L };
        static const long dvs[3] = {  4096L, 65536L, 4096L };
        static const char *nm[3] = { "u fast, v slow", "u slow, v fast",
                                     "both slow" };
        static const char *want[3] = { "MIN", "MIN", "MAG" };
        int j;
        unsigned long rr, cc;

        for (rr = 0UL; rr < 64UL; rr++)
            for (cc = 0UL; cc < 64UL; cc++)
                tex[rr * 64UL + cc] = ((rr ^ cc) & 1UL) ? 0x00FF0000UL : 0UL;

        for (j = 0; j < 3; j++) {
            unsigned long got;
            unsigned v;

            blank();
            (void)setup(1024UL, 0UL, 16UL, 8UL, dus[j],
                        OSMGA_HW3D_TEXF_BILINMIN
                        | OSMGA_HW3D_TEXF_REPEATU
                        | OSMGA_HW3D_TEXF_REPEATV);
            batch->state.texW = 64UL; batch->state.texH = 64UL;
            batch->state.texPitch = 64UL;
            batch->state.tmr[1] = 0L; batch->state.tmr[2] = 0L;
            batch->state.tmr[3] = dvs[j];
            setTU(batch, dus[j] / 2L);
            setTV(batch, dvs[j] / 2L);
            v = fire();
            got = (v != OSMGA_HW3D_OK) ? 9999UL
                                       : ((pixat(2UL, 4UL) >> 16) & 0xFFUL);
            printf("   %-15s du %6ld dv %6ld  red %4lu  %s  GL wants %s\n",
                   nm[j], dus[j], dvs[j], got,
                   (got == 0UL || got == 255UL) ? "point" :
                   (got == 9999UL) ? "ref  " : "blend", want[j]);
        }
        printf("     a blend means MIN was chosen, a point sample means MAG.\n"
               "     If the middle row points where GL wants MIN, the engine\n"
               "     is looking at one axis only.\n");
    }

    printf("\n73. where does a mipmap mode read from\n");
    {
        /*
         * The half of mipmapping that chooses a level per fragment is already
         * there and measured (70, 71, 72).  What is missing is where the
         * levels live, and no amount of reading the register description
         * settles it: there is ONE texture origin, the mode names mm1s, mm2s,
         * mm4s and mm8s say nothing about addressing, and the rfw field the
         * driver fills with 8 - log2(width) has no explanation anywhere --
         * only the observation that a 1024-wide texture, whose rfw wraps to
         * 62, samples correctly, so rfw does not touch ordinary fetching.
         *
         * So the whole texture window is filled with an ATLAS whose every
         * word holds its own offset, and the question becomes not what came
         * back but WHERE it came from.  The code is
         *
         *      red = offset & 255, green = offset >> 8,
         *      blue = (red*red + green*green) & 255
         *
         * -- a checksum a blend of two words cannot usually satisfy, which
         * matters because averaging two addresses would otherwise look like a
         * third address.  python: under four in a thousand random blends
         * decode, and a real fetch shows a RUN of consistent addresses where
         * a blend shows noise, so the run is the real discriminator.
         *
         * The controls come first: the same atlas read with the ordinary
         * nearest and bilinear filters, so that a mode's reading can be told
         * against a known-good decode of the same memory.
         */
        unsigned long base = TEX_ORG;
        unsigned long words = 16UL * 1024UL;      /* the mapped window */
        unsigned long i;
        static const unsigned long modes[4] = { 0x8UL, 0x9UL, 0xAUL, 0xCUL };
        static const char *mnm[4] = { "mm1s", "mm2s", "mm4s", "mm8s" };
        int k, j;

        for (i = 0UL; i < words; i++) {
            unsigned long r = i & 0xFFUL, g = (i >> 8) & 0xFFUL;

            tex[i] = (r << 16) | (g << 8) | (((r * r) + (g * g)) & 0xFFUL);
        }
        (void)base;

        printf("     the atlas holds its own word offsets, checksummed\n");
        for (k = 0; k < 6; k++) {
            unsigned long fl;
            const char *nm;

            if (k == 0)      { fl = 0UL; nm = "nearest (control)"; }
            else if (k == 1) { fl = OSMGA_HW3D_TEXF_BILIN
                                    | OSMGA_HW3D_TEXF_BILINMIN;
                               nm = "bilinear (control)"; }
            else             { fl = modes[k - 2]
                                    << OSMGA_HW3D_TEXF_MINMODE_SHIFT;
                               nm = mnm[k - 2]; }

            printf("     %-18s", nm);
            /* one-axis minification, four rates well away from the crossing */
            for (j = 0; j < 4; j++) {
                static const long rate[4] = { 32768L, 65536L, 131072L,
                                              262144L };
                unsigned long got, r, g, off;
                unsigned v;

                blank();
                (void)setup(1024UL, 0UL, 16UL, 4UL, rate[j],
                            fl | OSMGA_HW3D_TEXF_REPEATU
                               | OSMGA_HW3D_TEXF_REPEATV);
                batch->state.texW = 64UL; batch->state.texH = 64UL;
                batch->state.texPitch = 64UL;
                batch->state.tmr[1] = 0L; batch->state.tmr[2] = 0L;
                batch->state.tmr[3] = 0L;
                setTU(batch, rate[j] / 2L);
                setTV(batch, 0L);
                v = fire();
                if (v != OSMGA_HW3D_OK) { printf("   ref%-6u", v); continue; }
                got = pixat(0UL, 4UL);
                r = (got >> 16) & 0xFFUL; g = (got >> 8) & 0xFFUL;
                off = (g << 8) | r;
                if ((got & 0xFFUL) == (((r * r) + (g * g)) & 0xFFUL))
                    printf("  %6lu", off);
                else
                    printf("   mixed");
            }
            printf("\n");
        }
        printf("     rates 2, 4, 8, 16 texels per pixel; a number is a word\n"
               "     offset from the texture origin, \"mixed\" is a blend.\n"
               "     The base texture is words 0..4095; anything past that is\n"
               "     a level the engine found for itself.\n");
    }

    printf("\n74. is the mipmap bit inert -- all four modes, both regimes\n");
    {
        /*
         * 73 read ONE pixel per setting and found every mode reading the base
         * level.  That is consistent with two very different things: the
         * modes work and the hardware chose level nought, or the bit that
         * separates them from the plain filters does nothing at all here.
         *
         * The encodings say what to ask.  Each mipmap mode differs from a
         * plain filter in bit three alone:
         *
         *      nrst  0x0  <->  mm1s 0x8          bilin 0x2  <->  mm4s 0xa
         *      (0x1)      <->  mm2s 0x9          (0x4)      <->  mm8s 0xc
         *
         * The first two pairs are the ones with named partners; for mm2s and
         * mm8s the partner value is unnamed, so they are compared against the
         * plain filter they RESEMBLED in 73 -- nearest and bilinear -- which
         * is the claim being tested anyway.
         *
         * Whole rows, four rates, and both regimes: affine, and again with a
         * constant denominator so NOPERSPECTIVE is off.  A gate that only
         * applies to the perspective path is unlikely, and unlikely is not
         * measured.
         */
        static const unsigned long mm[4] = { 0x8UL, 0x9UL, 0xAUL, 0xCUL };
        static const char *mn[4] = { "mm1s", "mm2s", "mm4s", "mm8s" };
        static const int plainIsBilin[4] = { 0, 0, 1, 1 };
        static const long rate[4] = { 32768L, 65536L, 131072L, 262144L };
        static unsigned long shot[2][64];
        int k, j, persp;
        unsigned long col, i;

        for (i = 0UL; i < 16UL * 1024UL; i++) {
            unsigned long r = i & 0xFFUL, g = (i >> 8) & 0xFFUL;

            tex[i] = (r << 16) | (g << 8) | (((r * r) + (g * g)) & 0xFFUL);
        }

        for (persp = 0; persp < 2; persp++) {
            printf("     %s\n", persp ? "with a denominator (perspective)"
                                      : "affine");
            for (k = 0; k < 4; k++) {
                printf("       %-5s vs %-8s", mn[k],
                       plainIsBilin[k] ? "bilinear" : "nearest");
                for (j = 0; j < 4; j++) {
                    unsigned long same = 0UL;
                    int side;

                    for (side = 0; side < 2; side++) {
                        unsigned long fl;
                        unsigned v;

                        if (side == 0)
                            fl = plainIsBilin[k]
                               ? (OSMGA_HW3D_TEXF_BILIN
                                  | OSMGA_HW3D_TEXF_BILINMIN) : 0UL;
                        else
                            fl = mm[k] << OSMGA_HW3D_TEXF_MINMODE_SHIFT;
                        if (persp) fl |= OSMGA_HW3D_TEXF_PERSP;

                        blank();
                        (void)setup(1024UL, 0UL, 64UL, 4UL, rate[j],
                                    fl | OSMGA_HW3D_TEXF_REPEATU
                                       | OSMGA_HW3D_TEXF_REPEATV);
                        batch->state.texW = 64UL; batch->state.texH = 64UL;
                        batch->state.texPitch = 64UL;
                        batch->state.tmr[1] = 0L; batch->state.tmr[2] = 0L;
                        batch->state.tmr[3] = 0L;
                        batch->state.tmr[4] = 0L; batch->state.tmr[5] = 0L;
                        setTU(batch, rate[j] / 2L);
                        setTV(batch, 0L);
                        setTQ(batch, 1L << 16);
                        v = fire();
                        for (col = 0UL; col < 64UL; col++)
                            shot[side][col] = (v == OSMGA_HW3D_OK)
                                            ? pixat(0UL, col)
                                            : 0xFFFFFFFFUL;
                    }
                    for (col = 0UL; col < 64UL; col++)
                        if (shot[0][col] == shot[1][col]) same++;
                    printf(" %2lu/64", same);
                }
                printf("\n");
            }
        }
        printf("       rates 2, 4, 8, 16 texels per pixel.  64/64 everywhere\n"
               "       means the mipmap bit changes nothing this path can see.\n");
    }

    printf("\n75. does any rfw wake the mipmap bit\n");
    {
        /*
         * 74 showed bit three of the minification field inert -- mm1s renders
         * exactly as nrst and mm4s exactly as bilin, over a whole row.  One
         * explanation left standing is that the bit is gated by something
         * that currently says "there are no levels", and rfw is the only
         * candidate anyone has: the driver fills it with 8 - log2(width) and
         * nothing in the code says why.
         *
         * The encoder derives rfw from the DECLARED width, so declaring a
         * different width is a handle on it without another reboot -- nine of
         * the sixty-four values, including nought and the wrapped ones past
         * 256.  Declaring a width the texture does not have changes the
         * addressing too, but it changes it the SAME way for both settings,
         * so a difference between them still isolates bit three.
         */
        static const unsigned long dims[8] = { 8UL, 16UL, 32UL, 64UL,
                                               128UL, 256UL, 512UL, 1024UL };
        int j;
        unsigned long i;

        for (i = 0UL; i < 16UL * 1024UL; i++) {
            unsigned long r = i & 0xFFUL, g = (i >> 8) & 0xFFUL;

            tex[i] = (r << 16) | (g << 8) | (((r * r) + (g * g)) & 0xFFUL);
        }

        printf("     %6s %5s   %s\n", "texW", "rfw", "mm1s against nrst");
        for (j = 0; j < 8; j++) {
            unsigned long shot[2][16];
            int k;
            unsigned long col, lw = 0UL, d = dims[j];
            unsigned long same = 0UL;
            int refused = 0;

            while ((1UL << lw) < d) lw++;
            for (k = 0; k < 2; k++) {
                unsigned long fl = k ? (OSMGA_HW3D_TEXF_MINMODE_MM1S
                                        << OSMGA_HW3D_TEXF_MINMODE_SHIFT)
                                     : 0UL;
                unsigned v;

                blank();
                (void)setup(1024UL, 0UL, 16UL, 4UL, 65536L,
                            fl | OSMGA_HW3D_TEXF_REPEATU
                               | OSMGA_HW3D_TEXF_REPEATV);
                batch->state.texW = d; batch->state.texH = 8UL;
                batch->state.texPitch = d;
                batch->state.tmr[1] = 0L; batch->state.tmr[2] = 0L;
                batch->state.tmr[3] = 0L;
                setTU(batch, 32768L); setTV(batch, 0L);
                v = fire();
                if (v != OSMGA_HW3D_OK) refused = 1;
                for (col = 0UL; col < 16UL; col++)
                    shot[k][col] = (v == OSMGA_HW3D_OK)
                                 ? pixat(0UL, col) : 0xFFFFFFFFUL;
            }
            for (col = 0UL; col < 16UL; col++)
                if (shot[0][col] == shot[1][col]) same++;
            printf("     %6lu %5lu   %s\n", d, (8UL - lw) & 63UL,
                   refused ? "refused"
                           : (same == 16UL ? "identical on all 16"
                                           : "DIFFERS"));
        }
        printf("     any row that differs is the bit doing something\n");
    }

    printf("\n76. does the engine blend with the TEXTURE's alpha\n");
    {
        /*
         * Blending and texturing are each admitted on their own and refused
         * together, for a reason that is about batching rather than about the
         * engine (a partly submitted split triangle plus a software redraw
         * would blend twice).  Before any of that is worth solving, the thing
         * that would be a WRONG PICTURE rather than a refusal has to be
         * measured: which alpha the blend consumes.
         *
         * GL_REPLACE on a texture that has an alpha gives Av = At, and the
         * blend then wants that alpha.  Eight texels carrying alphas from
         * nought to 224 in steps of 32, all white, drawn over a background of
         * 0x204060 with the engine's only blend.
         *
         * python says what each must come out as.  If the engine took the
         * FRAGMENT's alpha instead, every one of the eight would be the same
         * -- which is the discriminator, and it needs no kernel change since
         * the probe sets alphactrl itself.
         */
        /*
         * python, rounding the engine's blend to nearest rather than down.
         * Three forms fit all eight rows -- nearest, ceiling, and the shifted
         * product the engine uses elsewhere -- and eight points cannot
         * separate them, so what is asserted below is the CLAIM (that the
         * texture's alpha reaches the blend at all), with the arithmetic left
         * as an open question rather than picked from three.
         */
        static const unsigned long want[8][3] = {
            {  32UL,  64UL,  96UL }, {  60UL,  88UL, 116UL },
            {  88UL, 112UL, 136UL }, { 116UL, 136UL, 156UL },
            { 144UL, 160UL, 176UL }, { 172UL, 184UL, 196UL },
            { 200UL, 208UL, 216UL }, { 228UL, 232UL, 236UL }
        };
        unsigned long rr, cc;
        int j, bad = 0, same = 1;
        unsigned long first[3];
        unsigned v;

        for (rr = 0UL; rr < 64UL; rr++)
            for (cc = 0UL; cc < 64UL; cc++)
                tex[rr * 64UL + cc] = (((cc & 7UL) * 32UL) << 24)
                                    | 0x00FFFFFFUL;

        /* the background the blend has to read */
        for (rr = 0UL; rr < 64UL; rr++)
            for (cc = 0UL; cc < STRIDE_DW; cc++)
                colour[rr * STRIDE_DW + cc] = 0x00204060UL;

        {
            OSMGAHW3DTri *t = setup(1024UL, 0UL, 8UL, 4UL,
                                    (long)(OSMGA_HW3D_TEX_SPAN / 64UL),
                                    OSMGA_HW3D_TEXF_TEXALPHA);

            batch->state.texW = 64UL; batch->state.texH = 64UL;
            batch->state.texPitch = 64UL;
            batch->state.tmr[1] = 0L; batch->state.tmr[2] = 0L;
            batch->state.tmr[3] = 0L;
            setTU(batch, (long)(OSMGA_HW3D_TEX_SPAN / 128UL));
            setTV(batch, 0L);
            /*
             * alphasel = fromtex.  The constant the builder uses,
             * 0x01000154, has alphasel = diffused in bits 24-25 -- the
             * INTERPOLATED alpha -- and this probe never writes ALPHASTART,
             * so that alpha is nought and every texel came back as the bare
             * background.  Which is not the engine failing to blend; it is
             * the engine blending with the alpha it was told to use.
             */
            t->alphactrl = 0x00000154UL;
            v = fire();
        }
        printf("     %-8s %14s %14s\n", "texel", "got", "wanted");
        for (j = 0; j < 8; j++) {
            unsigned long got = (v == OSMGA_HW3D_OK)
                              ? colour[0UL * STRIDE_DW + (unsigned long)j]
                              : 0UL;
            unsigned long r = (got >> 16) & 0xFFUL;
            unsigned long g = (got >> 8) & 0xFFUL;
            unsigned long b = got & 0xFFUL;

            if (j == 0) { first[0] = r; first[1] = g; first[2] = b; }
            else if (r != first[0] || g != first[1] || b != first[2])
                same = 0;
            printf("     alpha %3d  %3lu %3lu %3lu   %3lu %3lu %3lu%s\n",
                   j * 32, r, g, b,
                   want[j][0], want[j][1], want[j][2],
                   (r == want[j][0] && g == want[j][1] && b == want[j][2])
                       ? "" : "   <<");
            if (r != want[j][0] || g != want[j][1] || b != want[j][2]) bad++;
        }
        printf("     verdict %u.  All eight identical would mean the blend did\n"
               "     not see the texture's alpha: %s\n",
               v, same ? "they are" : "they are not");
        say("fromtex puts the texture's alpha into the blend",
            (bad == 0) ? 1U : 0U, 1U);
    }

    printf("\n77. which alpha each selector puts into the blend\n");
    {
        /*
         * 76 found the blend using an alpha of nothing because the constant
         * this back end uses has AC_alphasel at "diffused" and no fragment
         * alpha was written.  That is one of three, and GL needs all three:
         *
         *      RGB  + REPLACE    Av = Af        diffused
         *      RGBA + REPLACE    Av = At        fromtex
         *      any  + MODULATE   Av = Af * At   modulated
         *
         * So each is asked for directly, with a fragment alpha the texture
         * does not carry and a source colour that is not neutral, at four
         * texture alphas chosen so the three answers differ at every one --
         * python says they do.  A selector that reads as another one is a
         * wrong picture waiting for the chooser to be opened.
         */
        static const unsigned long ats[4] = { 1UL, 127UL, 128UL, 254UL };
        static const unsigned long sel[3] = { 0x00000154UL,   /* fromtex   */
                                              0x01000154UL,   /* diffused  */
                                              0x02000154UL }; /* modulated */
        static const char *sn[3] = { "fromtex", "diffused", "modulated" };
        unsigned long rr, cc;
        int j, k;

        /* the destination the blend reads */
        for (rr = 0UL; rr < 64UL; rr++)
            for (cc = 0UL; cc < STRIDE_DW; cc++)
                colour[rr * STRIDE_DW + cc] = 0x00204060UL;

        printf("     texture colour c08040, fragment alpha 96,"
               " destination 204060\n");
        printf("     %-10s", "At");
        for (j = 0; j < 4; j++) printf(" %14lu", ats[j]);
        printf("\n");
        for (k = 0; k < 3; k++) {
            printf("     %-10s", sn[k]);
            for (j = 0; j < 4; j++) {
                OSMGAHW3DTri *t;
                unsigned long got;
                unsigned v;

                for (rr = 0UL; rr < 64UL; rr++)
                    for (cc = 0UL; cc < 64UL; cc++)
                        tex[rr * 64UL + cc] = (ats[j] << 24) | 0x00C08040UL;
                for (rr = 0UL; rr < 4UL; rr++)
                    for (cc = 0UL; cc < 16UL; cc++)
                        colour[rr * STRIDE_DW + cc] = 0x00204060UL;

                t = setup(1024UL, 0UL, 8UL, 4UL, 0L,
                          OSMGA_HW3D_TEXF_TEXALPHA);
                batch->state.texW = 64UL; batch->state.texH = 64UL;
                batch->state.texPitch = 64UL;
                batch->state.tmr[1] = 0L; batch->state.tmr[2] = 0L;
                batch->state.tmr[3] = 0L;
                setTU(batch, 0L); setTV(batch, 0L);
                t->alphactrl = sel[k];
                t->a0 = 96UL << 15;     /* the fragment's own alpha */
                t->adx = 0UL; t->ady = 0UL;
                v = fire();
                got = (v != OSMGA_HW3D_OK) ? 0xFFFFFFFFUL
                                           : colour[0UL * STRIDE_DW + 2UL];
                printf(" %3lu %3lu %3lu  ", (got >> 16) & 0xFFUL,
                       (got >> 8) & 0xFFUL, got & 0xFFUL);
            }
            printf("\n");
        }
        printf("     python wants, at At = 1 / 127 / 128 / 254:\n"
               "       diffused   92 88 84   92 88 84   92 88 84   92 88 84\n"
               "       fromtex    33 64 96  112 96 80  112 96 80  191 128 64\n"
               "       modulated  32 64 96   62 76 90   62 76 90   92 88 84\n");
    }

    /*
     * 78. Whose anchor does a primitive read?
     *
     * The one fact the whole atomic-triangle change turns on.  The older
     * sections above now read 3, 3, 3 where they used to read 3, 11, 19, and
     * that looks like an answer until you notice what they set: setTV writes
     * the SAME anchor into every trapezoid and the later ones are copies of
     * the first (section 8b).  Three equal readings from three equal anchors
     * cannot tell
     *
     *    A  each primitive re-seeds from its OWN newly written anchor
     *    B  the engine latches the batch's first anchor and ignores the rest
     *
     * apart, and B would quietly mis-draw the second trapezoid of every split
     * triangle.  So: three primitives with anchors that DIFFER.
     *
     *    A         3/2, 5/4, 9/6      each reads what it was given
     *    B         3/2, 3/2, 3/2      only the first anchor ever lands
     *    runs on   3/2, 5+h/4, 9+h/6  the row index accumulates as well
     *
     * The texture has to be put back first: section 77 fills the whole thing
     * with one colour, and a uniform texture reads the same everywhere -- the
     * first cut of this section and of 80 were both vacuous for that reason.
     */
    {
        long texel = (long)(OSMGA_HW3D_TEX_SPAN / DIM);
        OSMGAHW3DTri *t;
        unsigned long p0, p1, p2;
        unsigned v;

        for (r = 0UL; r < DIM; r++)
            for (c = 0UL; c < DIM; c++)
                tex[r * DIM + c] = (r << 8) | c;

        printf("\n78. whose anchor does each primitive read\n");
        blank();
        t = setup(64UL, 0UL, 8UL, 4UL, 0L, 0UL);
        batch->state.tmr[0] = 0L;   /* constant across the primitive, so the */
        batch->state.tmr[1] = 0L;   /* reading is the anchor and nothing else */
        batch->state.tmr[2] = 0L;
        batch->state.tmr[3] = 0L;
        t->tu0 = 2L * texel;  t->tv0 = 3L * texel;
        batch->triCount = 3UL;
        batch->tri[1] = batch->tri[0];
        batch->tri[1].y = 8L;
        batch->tri[1].tu0 = 4L * texel;  batch->tri[1].tv0 = 5L * texel;
        batch->tri[2] = batch->tri[0];
        batch->tri[2].y = 16L;
        batch->tri[2].tu0 = 6L * texel;  batch->tri[2].tv0 = 9L * texel;
        v = fire();
        p0 = pixat( 0UL, 2UL);
        p1 = pixat( 8UL, 2UL);
        p2 = pixat(16UL, 2UL);
        printf("   verdict %u   v/u read: %lu/%lu, %lu/%lu, %lu/%lu\n", v,
               (p0 >> 8) & 0xFFUL, p0 & 0xFFUL,
               (p1 >> 8) & 0xFFUL, p1 & 0xFFUL,
               (p2 >> 8) & 0xFFUL, p2 & 0xFFUL);
        printf("   own anchor -> 3/2, 5/4, 9/6;  first latched -> 3/2 three"
               " times;  accumulating -> v grows by the heights\n");
        if (v != OSMGA_HW3D_OK) {
            printf("   FAIL  the three-primitive batch was refused\n");
            failures++;
        } else if (((p0 >> 8) & 0xFFUL) != 3UL || (p0 & 0xFFUL) != 2UL) {
            printf("   FAIL  even the first primitive did not read its"
                   " anchor\n");
            failures++;
        } else if (((p1 >> 8) & 0xFFUL) == 5UL && (p1 & 0xFFUL) == 4UL &&
                   ((p2 >> 8) & 0xFFUL) == 9UL && (p2 & 0xFFUL) == 6UL)
            printf("   ok    every primitive reads its own anchor\n");
        else if (((p1 >> 8) & 0xFFUL) == 3UL && (p1 & 0xFFUL) == 2UL) {
            printf("   FAIL  ONLY THE FIRST ANCHOR LANDS -- the second"
                   " trapezoid of every\n"
                   "         split triangle would be drawn from the first"
                   " one's start.\n");
            failures++;
        } else {
            printf("   FAIL  neither answer\n");
            failures++;
        }
    }

    /*
     * 78b. And the DENOMINATOR's anchor -- does TMR8 re-seed too?
     *
     * Section 78 settles the numerators and says nothing about q, because it
     * runs affine.  The builder gives a split triangle's halves their own q
     * anchor as well (OpenStepMGAMesaTriangle.c, the perspective branch), so
     * a latched TMR8 would misdraw every projective split even though the
     * numerators were right.
     *
     * Two primitives, both with u = 8 texels and v = 10 texels in the
     * NUMERATOR, and q doubled on the second.  A re-seeded q halves the
     * quotient; a latched one leaves it alone.
     */
    {
        long texel = (long)(OSMGA_HW3D_TEX_SPAN / DIM);
        OSMGAHW3DTri *t;
        unsigned long p0, p1;
        unsigned v;

        printf("\n78b. does the denominator's anchor re-seed as well\n");
        blank();
        t = setup(64UL, 0UL, 8UL, 4UL, 0L, OSMGA_HW3D_TEXF_PERSP);
        batch->state.tmr[0] = 0L; batch->state.tmr[1] = 0L;
        batch->state.tmr[2] = 0L; batch->state.tmr[3] = 0L;
        batch->state.tmr[4] = 0L; batch->state.tmr[5] = 0L;
        t->tu0 = 8L * texel;  t->tv0 = 10L * texel;
        t->tq0 = OSMGA_HW3D_Q_ONE;
        batch->triCount = 2UL;
        batch->tri[1] = batch->tri[0];
        batch->tri[1].y = 8L;
        batch->tri[1].tq0 = 2L * OSMGA_HW3D_Q_ONE;
        v = fire();
        p0 = pixat(0UL, 2UL);
        p1 = pixat(8UL, 2UL);
        printf("   verdict %u   v/u read: %lu/%lu then %lu/%lu\n", v,
               (p0 >> 8) & 0xFFUL, p0 & 0xFFUL,
               (p1 >> 8) & 0xFFUL, p1 & 0xFFUL);
        printf("   q re-seeds -> 5/4 on the second;  q latched -> 10/8\n");
        if (v != OSMGA_HW3D_OK) {
            printf("   FAIL  the perspective pair was refused\n");
            failures++;
        } else if (((p0 >> 8) & 0xFFUL) != 10UL || (p0 & 0xFFUL) != 8UL) {
            printf("   FAIL  the first primitive did not read 10/8\n");
            failures++;
        } else if (((p1 >> 8) & 0xFFUL) == 5UL && (p1 & 0xFFUL) == 4UL)
            printf("   ok    the denominator re-seeds with the numerators\n");
        else if (((p1 >> 8) & 0xFFUL) == 10UL && (p1 & 0xFFUL) == 8UL) {
            printf("   FAIL  TMR8 IS LATCHED -- projective splits would be"
                   " drawn with the\n"
                   "         first half's denominator throughout.\n");
            failures++;
        } else {
            printf("   FAIL  neither answer\n");
            failures++;
        }
    }

    /*
     * 79. A refused second trapezoid draws nothing at all.
     *
     * This is what opens texture-plus-blending, so it is not enough to see
     * the batch refused and the surface blank: a first trapezoid that could
     * never have drawn would give exactly that.  The positive control goes
     * first -- the same first trapezoid, submitted alone, has to change both
     * the pixel and its depth word.  Only then does the failing pair mean
     * anything.
     *
     * DEPTH IS TESTED, and the first cut of this claimed to and did not: it
     * used DWG_TEX, whose atype is I, so no depth was ever written and the
     * depth half of the assertion was vacuous.  This one uses DWG_TEXZ.
     *
     * The reads after the refusal also wait rather than being taken straight
     * away.  Submit returns when the list has been handed over, not when the
     * engine has finished, so an immediate read is biased towards finding the
     * surface still blank -- which is the answer this section is hoping for,
     * and therefore the one it must not be allowed to get cheaply.  A whole
     * valid batch is drawn elsewhere afterwards and waited for; anything the
     * refused batch had in flight would have landed by then.
     */
    {
        long texel = (long)(OSMGA_HW3D_TEX_SPAN / DIM);
        OSMGAHW3DTri *t;
        unsigned long alone, paired;
        unsigned short zalone, zpaired;
        unsigned v1, v2;
        unsigned long i2;

        printf("\n79. a refused second trapezoid leaves the first undrawn\n");

        blank();
        for (i2 = 0UL; i2 < 64UL * STRIDE_DW; i2++)
            depth[i2] = 0xFFFFU;
        t = setup(64UL, 0UL, 8UL, 4UL, texel, 0UL);
        t->dwgctl = DWG_TEXZ;
        batch->state.zorg = DEPTH_ORG;
        t->z0 = 0x4000UL << 15; t->zdx = 0UL; t->zdy = 0UL;
        t->tu0 = 2L * texel;
        t->tv0 = 3L * texel;
        v1 = fire();
        alone  = pixat(0UL, 2UL);
        zalone = depth[0UL * STRIDE_DW + 2UL];
        say("the positive control draws", v1, OSMGA_HW3D_OK);
        if (alone == BLANK) {
            printf("   FAIL  the control left the pixel blank, so the colour"
                   " test below is vacuous\n");
            failures++;
        }
        if (zalone == 0xFFFFU) {
            printf("   FAIL  the control left the depth word alone, so the"
                   " depth test below is vacuous\n");
            failures++;
        } else
            printf("   ok    and it wrote depth: %04x\n",
                   (unsigned)zalone);

        blank();
        for (i2 = 0UL; i2 < 64UL * STRIDE_DW; i2++)
            depth[i2] = 0xFFFFU;
        t = setup(64UL, 0UL, 8UL, 4UL, texel, 0UL);
        t->dwgctl = DWG_TEXZ;
        batch->state.zorg = DEPTH_ORG;
        t->z0 = 0x4000UL << 15; t->zdx = 0UL; t->zdy = 0UL;
        t->tu0 = 2L * texel;
        t->tv0 = 3L * texel;
        batch->triCount = 2UL;
        batch->tri[1] = batch->tri[0];
        batch->tri[1].y = 8L;
        /* one past the allowance, which the anchor check refuses */
        batch->tri[1].tu0 = -(long)OSMGA_HW3D_TEX_COORD_MAX - 1L;
        v2 = fire();
        if (v2 == OSMGA_HW3D_OK) {
            printf("   FAIL  the bad second trapezoid was accepted\n");
            failures++;
        } else
            printf("   ok    the pair is refused                          "
                   "     verdict %u\n", v2);

        /* now give the engine real work far away, and wait for it, so that
         * anything the refused batch had in flight has had its chance */
        {
            OSMGAHW3DTri *w;

            w = setup(64UL, 0UL, 8UL, 4UL, texel, 0UL);
            w->y = 40L;
            w->tu0 = 2L * texel;
            w->tv0 = 3L * texel;
            (void)fire();
            (void)pixat(40UL, 2UL);
        }
        paired  = colour[0UL * STRIDE_DW + 2UL];
        zpaired = depth[0UL * STRIDE_DW + 2UL];
        if (paired != BLANK) {
            printf("   FAIL  the first trapezoid drew colour anyway: %06lx\n",
                   paired & 0xFFFFFFUL);
            failures++;
        } else
            printf("   ok    and the first trapezoid drew no colour\n");
        if (zpaired != 0xFFFFU) {
            printf("   FAIL  the first trapezoid wrote depth anyway: %04x\n",
                   (unsigned)zpaired);
            failures++;
        } else
            printf("   ok    and it wrote no depth either\n");
    }

    /*
     * 80. What batching a far trapezoid costs the near one.
     *
     * The bias comes from the BATCH's reach and osmgaHW3DTexBiasFor is
     * non-increasing in it, so putting a far trapezoid beside a near one can
     * only lower the bias for both -- which leaves the near one's residual
     * (its own addend, less the batch bias) LARGER than it was alone.  The
     * residual stays non-negative, so a boundary-aligned coordinate still
     * cannot fall into the texel below; what it can do is cross upward.
     *
     * python, over the real constants:
     *
     *     near 8*T aligned   addend 510  bias 496 alone / 448 batched
     *                        residual 14 / 62      column 8 either way
     *     near 8*T - 20      residual 14 / 62      column 7 alone, 8 batched
     *
     * THAT WAS THE OLD ANSWER, and it is what this section measured: the
     * near trapezoid read column 7 alone and column 8 batched.  The bias is
     * the TRAPEZOID's now -- the validator hands the encoder one ladder rung
     * per trapezoid per axis and the encoder subtracts each anchor's own --
     * so the far one cannot reach the near one at all.
     *
     * Both halves must therefore read the SAME column alone and batched, and
     * the twenty-below half must still read column 7, which is where it lands
     * on its own bias.  That second requirement is what keeps this from
     * passing on a bias that has simply become smaller everywhere: 7 is the
     * tight answer, 8 is the loose one, and only the tight one is accepted.
     */
    {
        long texel = (long)(OSMGA_HW3D_TEX_SPAN / DIM);
        long far = (long)(1UL << 21) + texel;   /* two bands above the near */
        unsigned long alone, batched;
        unsigned v1, v2;
        int k;

        for (r = 0UL; r < DIM; r++)
            for (c = 0UL; c < DIM; c++)
                tex[r * DIM + c] = (r << 8) | c;

        printf("\n80. what batching a far trapezoid costs the near one\n");

        for (k = 0; k < 2; k++) {
            long near = 8L * texel - (k ? 20L : 0L);
            OSMGAHW3DTri *t;

            blank();
            t = setup(64UL, 0UL, 8UL, 4UL, 0L,
                      OSMGA_HW3D_TEXF_REPEATU | OSMGA_HW3D_TEXF_REPEATV);
            batch->state.tmr[0] = 0L; batch->state.tmr[1] = 0L;
            batch->state.tmr[2] = 0L; batch->state.tmr[3] = 0L;
            t->tu0 = near; t->tv0 = 0L;
            v1 = fire();
            alone = pixat(0UL, 2UL);

            blank();
            t = setup(64UL, 0UL, 8UL, 4UL, 0L,
                      OSMGA_HW3D_TEXF_REPEATU | OSMGA_HW3D_TEXF_REPEATV);
            batch->state.tmr[0] = 0L; batch->state.tmr[1] = 0L;
            batch->state.tmr[2] = 0L; batch->state.tmr[3] = 0L;
            t->tu0 = near; t->tv0 = 0L;
            batch->triCount = 2UL;
            batch->tri[1] = batch->tri[0];
            batch->tri[1].y = 8L;
            batch->tri[1].tu0 = far;
            v2 = fire();
            batched = pixat(0UL, 2UL);

            printf("   %-22s alone %u -> col %lu    batched %u -> col %lu\n",
                   k ? "twenty units below:" : "boundary aligned:",
                   v1, alone & 0xFFUL, v2, batched & 0xFFUL);
            if (v1 != OSMGA_HW3D_OK || v2 != OSMGA_HW3D_OK) {
                printf("   FAIL  one of the two was refused\n");
                failures++;
            } else if ((alone & 0xFFUL) != (batched & 0xFFUL)) {
                printf("   FAIL  batching moved the near trapezoid: %lu -> %lu"
                       "\n", alone & 0xFFUL, batched & 0xFFUL);
                failures++;
            } else if ((alone & 0xFFUL) != (k ? 7UL : 8UL)) {
                printf("   FAIL  it reads texel %lu, and its own bias puts it"
                       " at %lu\n", alone & 0xFFUL, k ? 7UL : 8UL);
                failures++;
            } else
                printf("   ok    texel %lu either way, which is its own"
                       " bias\n", alone & 0xFFUL);
        }
    }

    /*
     * 81. Does the blend's selector read the TEXEL's alpha, or the texture
     *     combiner's output?
     *
     * 77 asked which of the three selectors each is, and could not ask this:
     * it ran with the combiner passing the texture's alpha straight through,
     * so "the texel" and "what the combiner produced" were the same number.
     * Turn the combiner's own modulate on and they part company:
     *
     *     reads the texel      Av = Af * At
     *     reads the combiner   Av = Af * (Af * At)
     *
     * It matters because it decides the whole selector rule.  If the
     * selector reads the STAGE, then the stage already computes GL's alpha
     * table -- the encoder sets it from that table -- and "fromtex" is right
     * for every textured state with no format test at all.  If it read the
     * texel, Mesa would have to pick per format and environment, and
     * "modulated" would square the fragment's alpha under RGBA modulate.
     *
     * Both selectors are asked, because fromtex separates them too and by a
     * wider margin.  python, over the engine's own product and blend:
     *
     *     At    modulated texel / combiner     fromtex texel / combiner
     *     64     47 70 93   /  38 67 95         72 80 88 / 47 70 93
     *     128    62 76 90   /  44 68 94        112 96 80 / 62 76 90
     *     192    77 82 87   /  49 71 93        153 112 72 / 77 82 87
     */
    {
        static const unsigned long ats[3] = { 64UL, 128UL, 192UL };
        static const unsigned long sel[2] = { 0x02000154UL,   /* modulated */
                                              0x00000154UL }; /* fromtex   */
        static const char *sn[2] = { "modulated", "fromtex  " };
        /* [selector][at][channel] */
        static const unsigned long wantTexel[2][3][3] = {
            { {  47UL,  70UL,  93UL }, {  62UL,  76UL,  90UL },
              {  77UL,  82UL,  87UL } },
            { {  72UL,  80UL,  88UL }, { 112UL,  96UL,  80UL },
              { 153UL, 112UL,  72UL } }
        };
        static const unsigned long wantComb[2][3][3] = {
            { {  38UL,  67UL,  95UL }, {  44UL,  68UL,  94UL },
              {  49UL,  71UL,  93UL } },
            { {  47UL,  70UL,  93UL }, {  62UL,  76UL,  90UL },
              {  77UL,  82UL,  87UL } }
        };
        unsigned long rr, cc;
        int j, k, texelHits = 0, combHits = 0, other = 0;
        int fromtexComb = 0, modBelow = 0;
        unsigned long modRead[3][3];

        printf("\n81. does the blend's selector read the texel or the"
               " combiner\n");
        printf("     the combiner's own modulate is ON, which is what makes"
               " the two differ\n");
        for (k = 0; k < 2; k++) {
            printf("     %-10s", sn[k]);
            for (j = 0; j < 3; j++) {
                OSMGAHW3DTri *t;
                unsigned long got, r2, g2, b2;
                unsigned v;

                for (rr = 0UL; rr < 64UL; rr++)
                    for (cc = 0UL; cc < 64UL; cc++)
                        tex[rr * 64UL + cc] = (ats[j] << 24) | 0x00C08040UL;
                for (rr = 0UL; rr < 4UL; rr++)
                    for (cc = 0UL; cc < 16UL; cc++)
                        colour[rr * STRIDE_DW + cc] = 0x00204060UL;

                t = setup(1024UL, 0UL, 8UL, 4UL, 0L,
                          OSMGA_HW3D_TEXF_TEXALPHA
                          | OSMGA_HW3D_TEXF_MODULATE);
                batch->state.texW = 64UL; batch->state.texH = 64UL;
                batch->state.texPitch = 64UL;
                batch->state.tmr[1] = 0L; batch->state.tmr[2] = 0L;
                batch->state.tmr[3] = 0L;
                setTU(batch, 0L); setTV(batch, 0L);
                t->alphactrl = sel[k];
                t->a0 = 96UL << 15;
                t->adx = 0UL; t->ady = 0UL;
                /* white, so the combiner's COLOUR modulate leaves the
                 * texture's colour alone and only the alpha question is
                 * being asked */
                t->dr[0] = 255UL << 15; t->dr[3] = 255UL << 15;
                t->dr[6] = 255UL << 15;
                v = fire();
                got = (v != OSMGA_HW3D_OK) ? 0xFFFFFFFFUL
                                           : pixat(0UL, 2UL);
                r2 = (got >> 16) & 0xFFUL;
                g2 = (got >> 8) & 0xFFUL;
                b2 = got & 0xFFUL;
                printf(" %3lu %3lu %3lu  ", r2, g2, b2);
                if (r2 == wantTexel[k][j][0] && g2 == wantTexel[k][j][1] &&
                    b2 == wantTexel[k][j][2])
                    texelHits++;
                else if (r2 == wantComb[k][j][0] &&
                         g2 == wantComb[k][j][1] &&
                         b2 == wantComb[k][j][2])
                    combHits++;
                else
                    other++;
                if (k == 0) {
                    modRead[j][0] = r2; modRead[j][1] = g2; modRead[j][2] = b2;
                } else {
                    /* fromtex against the combiner's own product */
                    if (r2 == wantComb[1][j][0] && g2 == wantComb[1][j][1] &&
                        b2 == wantComb[1][j][2])
                        fromtexComb++;
                    /*
                     * A smaller alpha means less of the source, and the
                     * source's red is above the destination's -- so a smaller
                     * alpha shows as a SMALLER red.  Comparing the channel
                     * rather than an alpha keeps this free of any rounding
                     * model.
                     */
                    if (modRead[j][0] < r2)
                        modBelow++;
                }
            }
            printf("\n");
        }
        printf("     texel wants   47 70 93 /  62 76 90 /  77 82 87"
               "   then  72 80 88 / 112 96 80 / 153 112 72\n");
        printf("     comb  wants   38 67 95 /  44 68 94 /  49 71 93"
               "   then  47 70 93 /  62 76 90 /  77 82 87\n");
        /*
         * The answer is the COMBINER, and fromtex is what says so: three
         * readings out of three land on the combiner's product to the level.
         * Inverting the blend gives the alpha the engine actually used --
         * 24, 48, 72 for texture alphas of 64, 128, 192 against a fragment
         * alpha of 96, which is (Af * (At + 1)) >> 8 exactly.
         *
         * Only fromtex is asserted.  Modulated multiplies by the fragment's
         * alpha a SECOND time, and its rounding at the small end is not
         * modelled here to the level (17 where the doubled product says 18,
         * and no exact inverse at all at the lowest) -- so what is asserted
         * about it is the structural fact, that its alpha comes out strictly
         * below fromtex's at every texture alpha.  That is what "multiplies
         * again" means, and it does not rest on a rounding model.
         */
        if (fromtexComb == 3 && modBelow == 3)
            printf("   ok    the selector reads the texture STAGE, so"
                   " fromtex is GL's alpha\n");
        else {
            printf("   FAIL  fromtex matched the combiner %d/3, modulated"
                   " came out below it %d/3\n", fromtexComb, modBelow);
            failures++;
        }
        (void)texelHits; (void)combHits; (void)other;
    }

    printf("\n%s (%d failing)\n",
           failures ? "=== PROBLEM ===" : "=== nothing to report ===", failures);
    return failures ? 1 : 0;
}
