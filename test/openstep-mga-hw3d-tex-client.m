/*
 * openstep-mga-hw3d-tex-client.m -- M1-2g: texture through the batch path.
 *
 * D3-4a proved the texture unit by MMIO: a 64x64 texture whose texels
 * encode their own coordinates mapped identically onto a destination
 * rectangle, 4096 of 4096.  This asks the same question of the batch path,
 * so the control was measured before the test was written.
 *
 * It also closes a hole rather than only adding a feature.  The batch
 * carried texture fields that the encoder never emitted, so a textured
 * batch would have drawn with whatever texture state was left behind --
 * the same inheritance that once made every pixel of an image differ.
 *
 *   cc -O -Wall -o /tmp/osmga-hw3d-tex openstep-mga-hw3d-tex-client.m -lDriver
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

#define O_RDWR 2
#define PROT_READ 0x01
#define PROT_WRITE 0x02
#define MAP_SHARED 0x0001

#define DEV_PATH        "/dev/osmgavram"
#define SUBMIT_PARAM    "OSMGAHW3DSubmit"
#define SETTLE_PARAM    "OSMGAHW3DSettle"
#define STATUS_PARAM    "OSMGAHW3DStatus"
#define CMD_MMAP_BASE   0x40000000UL
/* The batch only: the driver no longer lets the command list be mapped,
 * because a client able to rewrite it after validation could put anything in
 * front of the engine. */
#define CMD_MMAP_LEN    ((int)OSMGA_HW3D_BATCH_BYTES)
#define COLOUR_ORG      (4UL * 1024UL * 1024UL)
#define TEX_ORG         (6UL * 1024UL * 1024UL)
#define STRIDE_DW       1024UL
#define DIM             64UL
/* The kernel clips to 120 rows, so the two bands cannot both be 64 tall.
 * The textured one has to be the full texture height; the flat one beside
 * it takes what is left. */
#define FLATH           56UL
#define SENTINEL        0x5A5A5A5AUL

#define DWG_TEX         (0x6UL | (0x7UL << 4))      /* TEXTURE_TRAP | atype I */
#define DWG_FLAT        (0x4UL | (0x7UL << 4))      /* TRAP | atype I */

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
    case OSMGA_HW3D_E_TEXORG:  return "texture origin";
    case OSMGA_HW3D_E_TEXSIZE: return "texture size";
    case OSMGA_HW3D_E_DWGCTL:  return "drawing control";
    case OSMGA_HW3D_E_TEXCOORD:return "texture coordinate";
    default:                   return "other";
    }
}

static unsigned
verdict(IODeviceMaster *m, unsigned objNum)
{
    unsigned st[4], n = 4;

    if ([m getIntValues:st forParameter:STATUS_PARAM objectNumber:objNum
            count:&n] != IO_R_SUCCESS)
        return 0xFFFFU;
    return st[0];
}

static void
rect(OSMGAHW3DTri *t, unsigned long y, unsigned long h, unsigned long dwgctl)
{
    memset(t, 0, sizeof *t);
    t->dwgctl = dwgctl;
    t->alphactrl = 0x00000101UL;
    t->y = (long)y;
    t->h = (long)h;
    t->ar0 = (long)h;
    t->ar6 = (long)h;
    t->fxbndry = (DIM << 16) | 0UL;
    t->dr[0] = 200UL << 15;             /* only visible when untextured */
    t->dr[3] = 100UL << 15;
    t->dr[6] =  50UL << 15;
}

static void
texState(OSMGAHW3DBatch *b)
{
    unsigned long step = 1UL << (20UL - 6UL);   /* 64 texels: 0x4000 */

    b->state.texorg = TEX_ORG;
    b->state.texW = DIM;
    b->state.texH = DIM;
    b->state.texPitch = DIM;
    b->state.texFormat = OSMGA_HW3D_TEXFMT_TW32;
    b->state.tmr[0] = step;             /* one texel per pixel in x */
    b->state.tmr[3] = step;             /* and in y */
    b->state.tmr[8] = 1UL << 16;        /* H, which takes no decal */
}

/*
 * ---- M1-3i step 0: the same batch, submitted two different ways ----
 *
 * Mesa submits by ioctl; everything measured so far went through the Mach
 * round trip instead.  The two were compared once by running two different
 * programs, which is no comparison at all: one drew a 380-pixel untextured
 * triangle and the other a textured band, so a difference in the numbers had
 * two explanations and the workload was the likelier one.
 *
 * Here the batch, the fill and the counting are all the same code; only the
 * call that hands it to the driver changes.  Both calls reach the same
 * -runHW3DSubmit, which the driver says in as many words, so any difference
 * that survives belongs to the submission and nothing else.
 */
#define SUBMIT_RPC      0
#define SUBMIT_IOCTL    1

static unsigned long
submitBatch(int how, int fd, IODeviceMaster *master, IOObjectNumber objNum)
{
    if (how == SUBMIT_IOCTL) {
        OSMGAHW3DSubmitBlock blk;

        blk.status = 0UL;
        blk.verdict = OSMGA_HW3D_NOT_RUN;
        blk.triangle = 0UL;
        blk.dwords = 0UL;
        blk.spins = 0UL;
        if (ioctl(fd, (long)OSMGA_IOC_SUBMIT, &blk) < 0)
            return OSMGA_HW3D_NOT_RUN;
        return blk.verdict;
    }
    (void)fd;
    (void)[master setIntValues:(unsigned *)0 forParameter:SUBMIT_PARAM
                  objectNumber:objNum count:0];
    return verdict(master, objNum);
}

int
main(int argc, char **argv)
{
    /*
     * "trust-driver" leaves out every read this program makes on its own
     * behalf -- before the submission as well as after it.  With them it
     * passes; without them it loses a pixel, and that difference is the
     * control against which the ioctl path is compared.
     */
    int noSettle = (argc > 1 && strcmp(argv[1], "trust-driver") == 0);
    /* argv[2]: the linear word index to read first after a submission.
     * argv[3]: how many processor-only iterations to burn before that. */
    unsigned long argFirst = 0UL, argSpin = 0UL;
    int argHaveFirst = 0;
    /* argv[4]: push the destination this many rows down the window, so that
     * the destination's first byte is no longer the window's first byte.
     * The stale 64 bytes then say which of the two they belong to. */
    unsigned long argRows = 0UL;
    /* argv[5]: start the MAPPING this many rows in, as well.  With this and
     * argv[4] both set to zero the destination sits on the driver's window
     * start; with this alone the destination is still the first thing in the
     * client's mapping but no longer the window's first byte.  That is the
     * pair that says whether the stale bytes belong to the mapping or to a
     * particular place in the card's memory. */
    unsigned long argMapRows = 0UL;
    /* argv[6]: "ioctl" to submit the way Mesa does; anything else keeps the
     * Mach round trip.  argv[7]: non-zero to put a priming submission in
     * front of EVERY trial, so every one of the forty is a state transition
     * rather than only the first. */
    int argHow = SUBMIT_RPC, argPrime = 0;
    /* argv[8]: what the DRIVER should read once a submission finishes.
     * 0 turns it off; N reads word N-1 of the window through the kernel's
     * uncached alias.  Whether that does what a client's own read does is
     * the whole question -- a client read past byte 64 settles everything
     * and one inside settles nothing, and it does not follow that a read
     * from the other side of the bus behaves the same way. */
    int argSettle = -1;
    IODeviceMaster *master;
    IOObjectNumber objNum;
    IOString kind;
    OSMGAHW3DBatch *batch;
    volatile unsigned long *colour, *tex;
    caddr_t cmd, cwin, twin;
    int fd;
    unsigned long row, col, ident = 0UL, drew = 0UL, dirty = 0UL, flat = 0UL;
    unsigned v, fails = 0U;
    IOReturn r;

    master = [IODeviceMaster new];
    if ([master lookUpByDeviceName:"Display0" objectNumber:&objNum
            deviceKind:&kind] != IO_R_SUCCESS) { printf("no Display0\n"); return 1; }
    if ((fd = open(DEV_PATH, O_RDWR)) < 0) { printf("no %s\n", DEV_PATH); return 1; }
    cmd  = mapDevice(fd, CMD_MMAP_BASE, CMD_MMAP_LEN);
    cwin = mapDevice(fd, COLOUR_ORG + argMapRows * STRIDE_DW * 4UL,
                     (int)((DIM + FLATH - argMapRows) * STRIDE_DW * 4UL));
    twin = mapDevice(fd, TEX_ORG,    (int)(DIM * DIM * 4UL));
    if (cmd == (caddr_t)-1 || cwin == (caddr_t)-1 || twin == (caddr_t)-1) {
        printf("a window will not map\n"); return 1;
    }
    batch  = (OSMGAHW3DBatch *)cmd;
    colour = (volatile unsigned long *)cwin;
    tex    = (volatile unsigned long *)twin;

    if (argc > 2) {
        argFirst = (unsigned long)atoi(argv[2]);
        argHaveFirst = 1;
    }
    if (argc > 3)
        argSpin = (unsigned long)atoi(argv[3]);
    if (argc > 4)
        argRows = (unsigned long)atoi(argv[4]);
    if (argc > 5)
        argMapRows = (unsigned long)atoi(argv[5]);
    if (argc > 6 && strcmp(argv[6], "ioctl") == 0)
        argHow = SUBMIT_IOCTL;
    if (argc > 7)
        argPrime = atoi(argv[7]);
    if (argc > 8)
        argSettle = atoi(argv[8]);

    if (argSettle >= 0) {
        unsigned one = (unsigned)argSettle;
        IOReturn sr = [master setIntValues:&one forParameter:SETTLE_PARAM
                              objectNumber:objNum count:1];

        printf("   driver settling read set to %d -> %s\n", argSettle,
               (sr == IO_R_SUCCESS) ? "accepted"
                                    : "REFUSED (the sweep below means nothing)");
        if (sr != IO_R_SUCCESS) return 1;
    }

    printf("M1-2g: texture through the batch path\n");

    /* Each texel carries its own coordinates, so the readback says which
     * texel arrived rather than only that something did. */
    for (row = 0UL; row < DIM; row++)
        for (col = 0UL; col < DIM; col++)
            tex[row * DIM + col] = (col << 16) | (row << 8) | 0x40UL;
    for (row = 0UL; row < DIM + FLATH; row++)
        for (col = 0UL; col < DIM; col++)
            colour[row * STRIDE_DW + col] = SENTINEL;

    /* One textured triangle and one flat one in the same batch: if the
     * texture state leaked into the flat band it would show as texels. */
    memset(batch, 0, sizeof *batch);
    batch->magic = OSMGA_HW3D_MAGIC;
    batch->version = OSMGA_HW3D_VERSION;
    batch->triCount = 2;
    batch->state.dstorg = COLOUR_ORG;
    /* The batch declares what it may touch; the kernel proves that lies
     * inside the window it owns and clips to it. */
    batch->state.dstWidth  = 64UL;
    batch->state.dstHeight = 120UL;
    batch->state.dstPitch  = 1024UL;   /* the display stride, as before */
    texState(batch);
    rect(&batch->tri[0], 0UL, DIM, DWG_TEX);
    rect(&batch->tri[1], DIM, FLATH, DWG_FLAT);

    r = [master setIntValues:(unsigned *)0 forParameter:SUBMIT_PARAM
                objectNumber:objNum count:0];
    printf("   submit returned %d, verdict %u (%s)\n", (int)r,
           verdict(master, objNum), why(verdict(master, objNum)));
    if (r != IO_R_SUCCESS) return 1;

    /*
     * PROBE: the same batch again, over freshly laid sentinels.  The forward
     * pass leaves exactly one pixel unwritten -- (0,0) -- while a reversed
     * pass afterwards writes all of them, and the coordinates turned out not
     * to be what distinguishes them.  What is left is that the second pass
     * runs with the texture unit already programmed.  If this second,
     * identical submission fills (0,0), the first pixel after the unit is
     * set up is being lost.
     */
    for (row = 0UL; row < DIM + FLATH; row++)
        for (col = 0UL; col < DIM; col++)
            colour[row * STRIDE_DW + col] = SENTINEL;
    /*
     * Read the region back before submitting.
     *
     * The pixel is lost rather than late -- five reads running all show the
     * sentinel -- and the first submission drew it correctly, so what
     * distinguishes this one is the fill that just happened.  If this
     * program's own writes are still on their way when the engine draws,
     * they land afterwards and put the sentinel back over what was drawn.
     * A read of the same memory should not let that happen.
     */
    if (!noSettle) {
        (void)colour[0];
        (void)colour[(DIM - 1UL) * STRIDE_DW];
    }

    (void)[master setIntValues:(unsigned *)0 forParameter:SUBMIT_PARAM
                  objectNumber:objNum count:0];

    /*
     * Is the missing pixel LATE or LOST?
     *
     * The driver's own uncached alias sees the sentinel at this submission's
     * completion, so both sides agree the engine's value is not there yet --
     * or never will be.  Reading the same word several times running tells
     * which: one that turns into the texel was late, and one that never does
     * was overwritten by this program's own fill landing after the engine
     * drew.
     */
    {
        unsigned long v0 = colour[0], v1 = colour[0], v2 = colour[0];
        unsigned long v3 = colour[0], v4 = colour[0];

        printf("   (0,0) five times running: %08lx %08lx %08lx %08lx %08lx\n",
               v0, v1, v2, v3, v4);
    }


    /*
     * PROBE: no preliminary read at all, and the band inspected BACKWARDS.
     *
     * (0,0) was the only stale pixel, and any prior read -- near it or far
     * from it -- made it appear.  That was taken to mean a particular write
     * arrives late.  It equally fits the first LOAD being early: if the
     * completion test is short by about the time of one read across the bus,
     * whichever location is looked at first is the one that sees the old
     * value.  Reversing the order tells the two apart, because a stale pixel
     * that follows the first place inspected is about time, and one that
     * stays at (0,0) is about the engine.
     */
    /*
     * Two reads thrown away before looking at anything that matters.
     *
     * Without them the first pixel inspected comes back holding what was
     * there before the draw.  It is not the address: reading the word next
     * door first shows THAT one stale too, and a moment later the checking
     * loop finds it correct.  It is not a stale cache line either, for the
     * same reason, and not the texture coordinates, and not the unit being
     * cold -- each was varied on its own.  What is left is that a read
     * arriving too soon after the submission returns sees the old value, and
     * the cost of one bus read is enough to settle it.
     *
     * This belongs in the driver rather than here; see REMAINING_WORK.
     */
    if (!noSettle) {
        (void)colour[40UL * STRIDE_DW + 40UL];
        (void)colour[50UL * STRIDE_DW + 50UL];
    }
    for (row = 0UL; row < DIM; row++)
        for (col = 0UL; col < DIM; col++) {
            unsigned long got = colour[row * STRIDE_DW + col];

            if (got != SENTINEL) drew++;

            if (got == tex[row * DIM + col]) ident++;
        }
    for (row = DIM; row < DIM + FLATH; row++)
        for (col = 0UL; col < DIM; col++)
            if (colour[row * STRIDE_DW + col] != SENTINEL) flat++;
    for (row = 0UL; row < DIM; row++)
        for (col = 0UL; col < DIM; col++)
            if (tex[row * DIM + col] != ((col << 16) | (row << 8) | 0x40UL))
                dirty++;

    printf("   textured band: drew %lu, identity %lu of %lu\n",
           drew, ident, DIM * DIM);
    printf("   flat band: drew %lu of %lu; its colour at (0,0) is %06lx "
           "(the interpolators say 0xc86432)\n",
           flat, FLATH * DIM, colour[DIM * STRIDE_DW] & 0xffffffUL);
    printf("   texture region modified: %lu words\n", dirty);

    /*
     * The same texture running the other way.  The validator was widened to
     * allow a negative gradient -- it used to refuse one outright, which
     * turned away roughly half of all real mapping -- but permitting it and
     * the engine interpolating it are different claims, and only the first
     * had been established.
     *
     * Start at the last texel and step backwards, so the texel at pixel x
     * should be column DIM-1-x.  Each texel carries its own column in its
     * red channel, so the readback says which texel arrived rather than only
     * that something did.
     */
    {
        unsigned long step = 1UL << (20UL - 6UL);
        unsigned long rev = 0UL, revBad = 0UL;

        for (row = 0UL; row < DIM; row++)
            for (col = 0UL; col < DIM; col++)
                colour[row * STRIDE_DW + col] = SENTINEL;

        batch->state.tmr[6] = (long)((DIM - 1UL) * step);
        batch->state.tmr[0] = -(long)step;
        r = [master setIntValues:(unsigned *)0 forParameter:SUBMIT_PARAM
                    objectNumber:objNum count:0];
        printf("   reversed pass: submit returned %d, verdict %u (%s)\n",
               (int)r, verdict(master, objNum), why(verdict(master, objNum)));
        if (r == IO_R_SUCCESS) {
            for (row = 0UL; row < DIM; row++)
                for (col = 0UL; col < DIM; col++) {
                    unsigned long got = colour[row * STRIDE_DW + col];
                    unsigned long want = ((DIM - 1UL - col) << 16) |
                                         (row << 8) | 0x40UL;

                    if (got == SENTINEL) continue;
                    rev++;
                    if (got != want) revBad++;
                }
            printf("   reversed: %lu drawn, %lu with the wrong texel\n",
                   rev, revBad);
            printf("   at row 0, columns 0/10/63 hold R = %lu/%lu/%lu "
                   "(want 63/53/0)\n",
                   (colour[0] >> 16) & 0xffUL,
                   (colour[10] >> 16) & 0xffUL,
                   (colour[63] >> 16) & 0xffUL);
        }
    }
    if (ident != DIM * DIM) fails++;
    if (flat != FLATH * DIM) fails++;
    if ((colour[DIM * STRIDE_DW] & 0xffffffUL) != 0x00c86432UL) fails++;
    if (dirty != 0UL) fails++;

    /* The validator, both ways round. */
    batch->state.texorg = 0UL;
    (void)[master setIntValues:(unsigned *)0 forParameter:SUBMIT_PARAM
                  objectNumber:objNum count:0];
    v = verdict(master, objNum);
    printf("   texorg at the visible framebuffer -> verdict %u (%s)\n",
           v, why(v));
    if (v != OSMGA_HW3D_E_TEXORG) fails++;

    batch->tri[0].dwgctl = DWG_FLAT;        /* nothing textured now */
    v = ([master setIntValues:(unsigned *)0 forParameter:SUBMIT_PARAM
                 objectNumber:objNum count:0] == IO_R_SUCCESS)
        ? OSMGA_HW3D_OK : verdict(master, objNum);
    printf("   the same bad texorg with no textured triangle -> verdict %u "
           "(%s)\n", v, why(v));
    if (v != OSMGA_HW3D_OK) fails++;

    /*
     * The coordinate bounds, on hardware rather than only in the unit
     * suite.  A refusal has to leave the machine as it was, not merely
     * return the right number, so each case is followed by a redraw that
     * must still land where it did before.
     */
    {
        static const struct { const char *what; int idx; long val; unsigned want; }
        cases[] = {
            { "a negative u start",            6, -1L,          OSMGA_HW3D_E_TEXCOORD },
            { "a negative v start",            7, -1L,          OSMGA_HW3D_E_TEXCOORD },
            { "a negative u increment",        0, -0x4000L,     OSMGA_HW3D_E_TEXCOORD },
            { "magnified eight times",         0, 0x4000L * 8L, OSMGA_HW3D_OK },
            { "magnified nine times",          0, 0x4000L * 9L, OSMGA_HW3D_E_TEXCOORD },
            { "the H family, which is ours",   4, -1L,          OSMGA_HW3D_OK },
        };
        unsigned long k;

        printf("   coordinate bounds, checked on hardware:\n");
        for (k = 0UL; k < sizeof cases / sizeof cases[0]; k++) {
            unsigned long before = colour[0];

            memset(batch, 0, sizeof *batch);
            batch->magic = OSMGA_HW3D_MAGIC;
            batch->version = OSMGA_HW3D_VERSION;
            batch->triCount = 1;
            batch->state.dstorg = COLOUR_ORG;
            /* The geometry the validator requires, re-set after the clear --
             * leaving it out made every case here fail on the pitch before
             * reaching the coordinate bound it was written to check. */
            batch->state.dstWidth  = 64UL;
            batch->state.dstHeight = 120UL;
            batch->state.dstPitch  = 1024UL;
            texState(batch);
            rect(&batch->tri[0], 0UL, DIM, DWG_TEX);
            batch->state.tmr[cases[k].idx] = cases[k].val;

            (void)[master setIntValues:(unsigned *)0 forParameter:SUBMIT_PARAM
                          objectNumber:objNum count:0];
            v = verdict(master, objNum);
            printf("      %-28s -> verdict %2u (%s)%s\n", cases[k].what, v,
                   why(v), (v == cases[k].want) ? "" : "   <-- WRONG");
            if (v != cases[k].want) fails++;

            /* A refused batch must not have drawn: pixel (0,0) still holds
             * whatever the last accepted draw put there. */
            if (cases[k].want != OSMGA_HW3D_OK && colour[0] != before) {
                printf("      ...and it CHANGED the framebuffer\n");
                fails++;
            }
        }
    }

    /*
     * The same count the ioctl path reports, so the two are comparable.
     *
     * The fault is intermittent -- this program gave every pixel on one run
     * and lost one on the next -- so a few passes settle nothing.  Forty
     * trials of exactly the shape the C client runs: lay sentinels, submit,
     * count, with no read of the destination in between when the driver is
     * being trusted on its own.  The repetition lives here rather than in a
     * shell loop because loops over this link do not survive.
     */
    {
        unsigned long trial, pass, shortRuns = 0UL, worst = DIM * DIM;
        unsigned long lost[40], nlost = 0UL;
        /*
         * Which destination word to touch FIRST, and how long to wait on the
         * processor alone before touching anything.
         *
         * The stale words came out as bytes 0..63, which was read as one
         * 64-byte transfer.  It is not safe to read it that way: the counting
         * pass walks the rectangle in order, so bytes 0..63 are also simply
         * the first sixteen loads it performs.  An older round of this
         * investigation had already found that reading a FAR AWAY word cured
         * the staleness, which says the window is time, not place -- and that
         * finding got lost.
         *
         * So: name the word to look at first.  If staleness follows the
         * choice, the address means nothing.  And spin on the processor
         * without touching the card: if that cures it too, what settles the
         * memory is elapsed time rather than the read's own bus traffic.
         */
        unsigned long firstIdx = argFirst, spin = argSpin;
        /* The rectangle, wherever it has been pushed to. */
        volatile unsigned long *dst = colour + argRows * STRIDE_DW;
        /* Where the priming batch draws: far enough down the driver's window
         * to miss everything this program looks at, including the row just
         * past the rectangle. */
        unsigned long tOrg, pOrg = COLOUR_ORG + 200UL * STRIDE_DW * 4UL;
        int haveFirst = argHaveFirst, firstStale = 0;
        /*
         * Counted separately from the short runs, because the two are not
         * the same question.  A word that is stale when it is the first
         * thing touched, and correct a moment later when the counting loop
         * arrives, never shows up as a short run at all -- so short runs
         * alone would report an address as innocent merely because it was
         * looked at early enough to be cured before it was counted.
         */
        unsigned long firstStaleCount = 0UL;

        memset(batch, 0, sizeof *batch);
        batch->magic = OSMGA_HW3D_MAGIC;
        batch->version = OSMGA_HW3D_VERSION;
        batch->triCount = 1;
        tOrg = COLOUR_ORG + (argMapRows + argRows) * STRIDE_DW * 4UL;
        batch->state.dstorg = tOrg;
        batch->state.dstWidth  = 64UL;
        /* The claim has to shrink by as much as the origin moved, or its
         * reach runs past the end of the window and the kernel refuses it. */
        batch->state.dstHeight = 120UL - argRows - argMapRows;
        batch->state.dstPitch  = 1024UL;
        texState(batch);
        rect(&batch->tri[0], 0UL, DIM, DWG_TEX);

        /*
         * Twice through, in one process.
         *
         * Every arrangement tried so far leaves exactly one short trial and
         * it is always the first, whichever way the batch is submitted and
         * whatever is submitted in front of it.  The program has already made
         * many accepted submissions and already read the whole band before
         * this loop is entered, so "the first draw of the process" does not
         * describe it either.  Running the identical loop a second time says
         * whether the thing is spent once and gone, or whether entering the
         * loop is itself what arms it.
         */
        for (pass = 0UL; pass < 2UL; pass++) {
        shortRuns = 0UL; worst = DIM * DIM; firstStaleCount = 0UL;
        for (trial = 0UL; trial < 40UL; trial++) {
            unsigned long n = 0UL;

            /*
             * A priming submission in front of every trial, so that the
             * question "what makes the first one different" can be asked
             * one variable at a time.
             *
             *   1  same batch but drawn somewhere else -- a state change
             *      with a different destination
             *   2  the very same batch again -- no state change at all
             *   3  the same destination, one texture register different --
             *      a state change with the destination left alone
             *
             * Mode 1 did not stop the fault, which is what killed the idea
             * that any change of register values was enough.
             */
            if (argPrime == 1) {
                batch->state.dstorg = pOrg;
                (void)submitBatch(argHow, fd, master, objNum);
                batch->state.dstorg = tOrg;
            }
            else if (argPrime == 2) {
                (void)submitBatch(argHow, fd, master, objNum);
            }
            else if (argPrime == 4) {
                /*
                 * A submission the kernel REFUSES, which therefore draws
                 * nothing.
                 *
                 * The one short trial is always the first of a pass, and a
                 * second pass through the identical loop has none at all --
                 * so whatever this is, it is spent once.  What sits between
                 * the program's last full read of the band and that first
                 * trial is the validator section, which is nothing but
                 * refused submissions.  If a refusal is what arms it, doing
                 * one in front of every trial should make every trial short.
                 */
                unsigned long keep = batch->state.dstPitch;

                batch->state.dstPitch = 0UL;
                (void)submitBatch(argHow, fd, master, objNum);
                batch->state.dstPitch = keep;
            }
            else if (argPrime == 3) {
                unsigned long keep = batch->state.tmr[0];

                batch->state.tmr[0] = keep / 2UL;
                (void)submitBatch(argHow, fd, master, objNum);
                batch->state.tmr[0] = keep;
            }
            /*
             * Modes 5 and 6 submit nothing.  They read ONE word, and the
             * question they ask is about the reading rather than about the
             * drawing.
             *
             * Every arrangement of priming submissions left the number
             * untouched, and the one short trial is always the first of a
             * pass.  What sits in front of it, and in front of nothing else,
             * is the validator section, whose only touch of this mapping is
             * a single load of word zero.  Everywhere else the last thing
             * read before a submission is the tail of the previous counting
             * pass, which is a high address.
             *
             * So the two modes put a single load where that difference lies:
             * one inside the first sixty-four bytes, one past them.
             *
             *   5  read dst[0]   -- expected to make every trial short
             *   6  read dst[16]  -- expected to make none of them short
             */
            if (argPrime == 5)
                (void)dst[0];
            else if (argPrime == 6)
                (void)dst[16];

            /* One row past the rectangle as well.  If the mapping's
             * offset were ignored, the client's base would still be the
             * window start while the engine honoured a dstorg further in --
             * and the proof of that is the rectangle landing one row lower
             * than the client is looking, which shows up here. */
            for (row = 0UL; row <= DIM; row++)
                for (col = 0UL; col < DIM; col++)
                    dst[row * STRIDE_DW + col] = SENTINEL;
            if (!noSettle) {
                (void)dst[0];
                (void)dst[(DIM - 1UL) * STRIDE_DW];
            }
            /*
             * Mode 7 is the falsifier.
             *
             * The pair of reads just above cures the fault, and has since the
             * beginning.  If what cures it is the LAST read before the
             * submission, then the cure belongs to the second of them -- the
             * one at a high address -- and keeping only the first should stop
             * curing anything.  If keeping only the first still cures, the
             * account is wrong.
             */
            else if (argPrime == 7)
                (void)dst[0];
            if (submitBatch(argHow, fd, master, objNum) != OSMGA_HW3D_OK) {
                printf("      trial %lu was refused\n", trial);
                fails++;
                continue;
            }
            /*
             * Record where, in the same pass that counts.
             *
             * A second look was no good: by the time it ran, the counting
             * pass had already been over every word and they all read
             * correctly.  Whatever settles this happens on first contact, so
             * the offsets have to be taken on first contact too.
             */
            if (spin != 0UL) {
                volatile unsigned long sink = 0UL;
                unsigned long q;

                for (q = 0UL; q < spin; q++) sink += q;
            }
            if (haveFirst) {
                firstStale = (colour[firstIdx] == SENTINEL);
                if (firstStale) firstStaleCount++;
            }

            nlost = 0UL;
            for (row = 0UL; row < DIM; row++)
                for (col = 0UL; col < DIM; col++) {
                    unsigned long idx = row * STRIDE_DW + col;

                    if (dst[idx] != SENTINEL) { n++; continue; }
                    if (nlost < 40UL) lost[nlost] = idx;
                    nlost++;
                }
            if (n != DIM * DIM) {
                shortRuns++;
                if (n < worst) worst = n;
                /* The trial number matters on its own: the counting pass
                 * leaves every line of the rectangle resident, so trial 0 is
                 * the only one whose fill writes to cold lines -- and every
                 * single-run observation that ever showed this fault was a
                 * cold one. */
                {
                    unsigned long k, runs = 0UL, cap = nlost;

                    if (cap > 40UL) cap = 40UL;
                    printf("      trial %lu came up %lu short", trial, nlost);
                    if (haveFirst)
                        printf("; the word looked at first (index %lu, "
                               "byte %lu) was %s", firstIdx, firstIdx * 4UL,
                               firstStale ? "STALE" : "already correct");
                    if (spin != 0UL)
                        printf("; %lu processor-only iterations first", spin);
                    printf(", at:\n");
                    for (k = 0UL; k < cap; k++) {
                        unsigned long off = lost[k] * 4UL;

                        if (k == 0UL || lost[k] != lost[k - 1UL] + 1UL)
                            runs++;
                        printf("         byte %7lu  x=%2lu y=%2lu  "
                               "mod32=%2lu mod64=%2lu\n", off,
                               lost[k] % STRIDE_DW, lost[k] / STRIDE_DW,
                               off & 31UL, off & 63UL);
                    }
                    printf("         in %lu contiguous stretch(es)\n", runs);
                }
            }
        }
        printf("   pass %lu: through the %s%s, %s", pass,
               argHow == SUBMIT_IOCTL ? "ioctl" : "RPC",
               argPrime ? " (primed every trial)" : "", noSettle
               ? "no read between fill and submit"
               : "with the client settling too");
        (void)0;
        if (haveFirst) printf(", first word read = index %lu", firstIdx);
        if (spin != 0UL) printf(", %lu spins first", spin);
        printf(": %lu of 40 runs short, worst %lu of %lu\n",
               shortRuns, worst, DIM * DIM);
        {
            unsigned long past = 0UL;

            for (col = 0UL; col < DIM; col++)
                if (dst[DIM * STRIDE_DW + col] != SENTINEL) past++;
            printf("   the row just past the rectangle holds %lu drawn "
                   "pixels (0 means the rectangle landed where it was "
                   "asked to)\n", past);
        }
        }   /* pass */
        if (haveFirst)
            printf("   and the word touched first was stale on %lu of 40\n",
                   firstStaleCount);
    }

    if (dirty != 0UL)
        printf("STOP -- the texture region was written to\n");
    else if (fails != 0U)
        printf("FAIL -- %u checks disagreed\n", fails);
    else
        printf("PASS -- every pixel took its own texel through the batch "
               "path, the flat triangle beside it stayed flat, and the "
               "texture origin is checked only when something is "
               "textured\n");
    (void)close(fd);
    return 0;
}
