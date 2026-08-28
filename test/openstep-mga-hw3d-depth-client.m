/*
 * openstep-mga-hw3d-depth-client.m -- M1-2e: depth through the batch path.
 *
 * Depth was proven by MMIO in D3-3c and proven to follow sloped edges in
 * D3-4b.  What has never run is the batch path: state.zorg, the per-triangle
 * z fields, the encoder's ZORG and DR0/DR2/DR3 emission, and the validator's
 * depth-origin check.  This exercises all four.
 *
 * The depth buffer is cleared by this client through its own VRAM mapping.
 * The batch has no fill primitive and should not gain one -- allowing atype
 * RPL would widen the opcode set the validator keeps narrow.  Depth is
 * 16-bit elements at PITCH stride, measured in D3-3; clearing it with the
 * colour buffer's geometry is the bug D3-3a made in the kernel and there is
 * no reason to repeat it here.
 *
 * M1-2f moved dwgctl and alphactrl into the triangle, so the four bands
 * that once needed four submissions now go as ONE batch.  That is the
 * point of this version: the result must be identical to the four-
 * submission one, and an encoder that reused any single triangle's dwgctl
 * for all of them would produce a different draw pattern -- NNYY, YYYY or
 * YYNN against the correct YYNY.
 *
 *   cc -O -Wall -o /tmp/osmga-hw3d-depth openstep-mga-hw3d-depth-client.m -lDriver
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

#define CMD_MMAP_BASE   0x40000000UL
/* The batch only: the driver no longer lets the command list be mapped,
 * because a client able to rewrite it after validation could put anything in
 * front of the engine. */
#define CMD_MMAP_LEN    ((int)OSMGA_HW3D_BATCH_BYTES)
/*
 * These were compile-time constants -- colour at 4 MiB, depth at 5 MiB --
 * and the VRAM window has since moved above them.  At 1600x1200x32 the
 * visible framebuffer alone is 7.68 MB, the window starts at 8.89 MiB, and
 * both constants sit below it, so this test died at "a window will not map"
 * and had been dying that way for as long as the mode was that large.
 *
 * Read from the driver now, which is the only thing that knows.
 */
static unsigned long osmgaColourOrg;
#define COLOUR_ORG      osmgaColourOrg
static unsigned long osmgaDepthOrg;
#define DEPTH_ORG       osmgaDepthOrg
#define STRIDE_DW       1024UL              /* 1024x768x4 */
#define CLIP_COLS       64UL
#define BAND            20UL
#define NBAND           4UL
#define ROWS            ((NBAND + 1UL) * BAND)      /* + a guard band */
#define SENTINEL        0x5A5A5A5AUL
#define ZCLEAR          0x8000U
#define ZGUARD          0xC0DEU

/* atype I is 7 << 4, ZI is 3 << 4; zmode lives in bits 8-10. */
/* Masked form: opcode in bits 0-3, access type in 4-6, z mode in 8-10.
 * Everything else in DWGCTL is the kernel's and cannot be expressed here. */
#define DWG_I           (0x4UL | (0x7UL << 4))
#define DWG_ZI          (0x4UL | (0x3UL << 4))
#define ZMODE_NOZCMP    0x000UL
#define ZMODE_ZLT       0x400UL
#define ZMODE_ZGTE      0x700UL

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
    case OSMGA_HW3D_E_DSTORG:   return "destination origin";
    case OSMGA_HW3D_E_ZORG:     return "depth origin";
    case OSMGA_HW3D_E_TEXORG:   return "texture origin";
    case OSMGA_HW3D_E_DWGCTL:   return "drawing control";
    case OSMGA_HW3D_E_TRIROW:   return "triangle rows";
    case OSMGA_HW3D_E_TRICOL:   return "triangle columns";
    case OSMGA_HW3D_E_TRISLOPE: return "edge slope";
    case OSMGA_HW3D_E_DSTSIZE:  return "destination size";
    case OSMGA_HW3D_E_DSTPITCH: return "destination pitch";
    default:                    return "other";
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
fillRect(OSMGAHW3DTri *t, unsigned long y, unsigned long h, unsigned long z,
         unsigned long dwgctl)
{
    memset(t, 0, sizeof *t);
    t->dwgctl = dwgctl;
    t->alphactrl = 0x00000101UL;
    t->y = (long)y;
    t->h = (long)h;
    t->ar0 = (long)h;                       /* axis-aligned: no slope */
    t->ar6 = (long)h;
    t->fxbndry = (CLIP_COLS << 16) | 0UL;
    t->dr[0] = 200UL << 15;
    t->dr[3] = 100UL << 15;
    t->dr[6] =  50UL << 15;
    t->z0 = z << 15;                        /* DR0 = depth << 15, measured */
}

int
main(void)
{
    IODeviceMaster *master;
    IOObjectNumber objNum;
    IOString kind;
    OSMGAHW3DBatch *batch;
    volatile unsigned long *colour;
    volatile unsigned short *depth;
    caddr_t cmd, cwin, dwin;
    int fd;
    unsigned long b, row, col;
    unsigned long drew[NBAND], zbad[NBAND], guardC = 0UL, guardZ = 0UL;
    unsigned v, fails = 0U;
    static const unsigned long zmode[NBAND] =
        { ZMODE_NOZCMP, ZMODE_ZLT, ZMODE_ZLT, ZMODE_ZGTE };
    static const unsigned long zval[NBAND] =
        { 0x7000UL, 0x4000UL, 0xC000UL, 0xC000UL };
    /* What the depth buffer must hold afterwards: the value we asked for
     * where the band drew, and the clear where it was rejected. */
    static const unsigned      zwant[NBAND] =
        { 0x7000U, 0x4000U, ZCLEAR, 0xC000U };
    static const int           wantDraw[NBAND] = { 1, 1, 0, 1 };

    master = [IODeviceMaster new];
    if ([master lookUpByDeviceName:"Display0" objectNumber:&objNum
            deviceKind:&kind] != IO_R_SUCCESS) {
        printf("Display0 not found\n");
        return 1;
    }
    {   /* Where the window actually is. */
        unsigned caps[OSMGA_HW3D_CAPS_COUNT];
        unsigned ncaps = OSMGA_HW3D_CAPS_COUNT;

        if ([master getIntValues:caps forParameter:OSMGA_HW3D_CAPS_PARAM
                    objectNumber:objNum count:&ncaps] != IO_R_SUCCESS ||
            ncaps != OSMGA_HW3D_CAPS_COUNT) {
            printf("capabilities unavailable\n"); return 1;
        }
        osmgaColourOrg = (unsigned long)caps[OSMGA_HW3D_CAP_VRAMOFF];
        /* A MiB above colour: it keeps the two apart, and stays
         * 128-aligned as ZORG requires because the window base is. */
        osmgaDepthOrg = osmgaColourOrg + 1024UL * 1024UL;
    }

    if ((fd = open(DEV_PATH, O_RDWR)) < 0) {
        printf("%s will not open\n", DEV_PATH);
        return 1;
    }
    cmd  = mapDevice(fd, CMD_MMAP_BASE, CMD_MMAP_LEN);
    cwin = mapDevice(fd, COLOUR_ORG, (int)(ROWS * STRIDE_DW * 4UL));
    dwin = mapDevice(fd, DEPTH_ORG,  (int)(ROWS * STRIDE_DW * 2UL));
    if (cmd == (caddr_t)-1 || cwin == (caddr_t)-1 || dwin == (caddr_t)-1) {
        printf("a window will not map (cmd %d colour %d depth %d)\n",
               cmd != (caddr_t)-1, cwin != (caddr_t)-1, dwin != (caddr_t)-1);
        return 1;
    }
    batch  = (OSMGAHW3DBatch *)cmd;
    colour = (volatile unsigned long *)cwin;
    depth  = (volatile unsigned short *)dwin;

    printf("M1-2e/2f: depth through the batch path, one batch\n");

    for (row = 0UL; row < ROWS; row++)
        for (col = 0UL; col < CLIP_COLS; col++) {
            colour[row * STRIDE_DW + col] = SENTINEL;
            /* 16-bit elements at PITCH stride, which is what D3-3 measured;
             * the guard band gets a value the clear never uses so that
             * "untouched" is unambiguous. */
            depth[row * STRIDE_DW + col] =
                (row < NBAND * BAND) ? ZCLEAR : ZGUARD;
        }

    /* The user mapping's cache attribute is not established for this path,
     * and comparing against a clear that never reached memory would make
     * every later number meaningless. */
    if (depth[0] != ZCLEAR || depth[NBAND * BAND * STRIDE_DW] != ZGUARD) {
        printf("   FAIL -- the depth clear did not read back (%04x, %04x)\n",
               depth[0], depth[NBAND * BAND * STRIDE_DW]);
        return 1;
    }
    printf("   depth cleared to %04x, guard %04x, both read back\n",
           ZCLEAR, ZGUARD);

    /* All four bands in ONE batch, each triangle carrying its own z mode. */
    memset(batch, 0, sizeof *batch);
    batch->magic = OSMGA_HW3D_MAGIC;
    batch->version = OSMGA_HW3D_VERSION;
    batch->triCount = NBAND;
    batch->state.dstorg = COLOUR_ORG;
    /* The batch declares what it may touch; the kernel proves that lies
     * inside the window it owns and clips to it. */
    batch->state.dstWidth  = 64UL;
    batch->state.dstHeight = 120UL;
    batch->state.dstPitch  = 1024UL;   /* the display stride, as before */
    batch->state.zorg = DEPTH_ORG;
    for (b = 0UL; b < NBAND; b++)
        fillRect(&batch->tri[b], b * BAND, BAND, zval[b], DWG_ZI | zmode[b]);
    /* Bits outside the client's mask must be ignored, not refused: this
     * triangle asks for linear addressing, SOLID and ARZERO as well. */
    batch->tri[0].dwgctl |= 0x80UL | 0x800UL | 0x1000UL;

    {
        IOReturn r = [master setIntValues:(unsigned *)0
                             forParameter:SUBMIT_PARAM
                             objectNumber:objNum count:0];
        if (r != IO_R_SUCCESS) {
            printf("   the batch was refused (%d, verdict %u %s)\n",
                   (int)r, verdict(master, objNum),
                   why(verdict(master, objNum)));
            return 1;
        }
        printf("   one batch of %lu triangles accepted\n", NBAND);
    }

    for (b = 0UL; b < NBAND; b++) {
        drew[b] = 0UL;
        zbad[b] = 0UL;
        for (row = b * BAND; row < (b + 1UL) * BAND; row++)
            for (col = 0UL; col < CLIP_COLS; col++) {
                if (colour[row * STRIDE_DW + col] != SENTINEL) drew[b]++;
                if (depth[row * STRIDE_DW + col] != zwant[b]) zbad[b]++;
            }
    }

    printf("   %-22s %8s %8s %8s\n", "band", "drew", "want", "depth!=want");
    for (b = 0UL; b < NBAND; b++) {
        const char *name = (b == 0UL) ? "NOZCMP z=7000"
                         : (b == 1UL) ? "ZLT near z=4000"
                         : (b == 2UL) ? "ZLT far  z=C000"
                                      : "ZGTE far z=C000";
        unsigned long want = wantDraw[b] ? BAND * CLIP_COLS : 0UL;

        printf("   %-22s %8lu %8lu %8lu%s\n", name, drew[b], want, zbad[b],
               (drew[b] == want && zbad[b] == 0UL) ? "" : "   <-- WRONG");
        if (drew[b] != want || zbad[b] != 0UL) fails++;
    }

    for (row = NBAND * BAND; row < ROWS; row++)
        for (col = 0UL; col < CLIP_COLS; col++) {
            if (colour[row * STRIDE_DW + col] != SENTINEL) guardC++;
            if (depth[row * STRIDE_DW + col] != ZGUARD) guardZ++;
        }
    printf("   guard: colour %lu, depth %lu\n", guardC, guardZ);

    /* The validator, both ways round.
     *
     * The destination size and pitch have to be here.  They were not, for as
     * long as this probe sat outside the build script and nobody ran it, and
     * a batch that leaves them at nought is refused for its PITCH -- verdict
     * 16 -- long before the validator reaches the depth origin these two
     * cases are about.  Both then "failed" for a reason that had nothing to
     * do with what they were asking. */
    memset(batch, 0, sizeof *batch);
    batch->magic = OSMGA_HW3D_MAGIC;
    batch->version = OSMGA_HW3D_VERSION;
    batch->triCount = 1;
    batch->state.dstorg = COLOUR_ORG;
    batch->state.dstWidth  = 64UL;
    batch->state.dstHeight = 120UL;
    batch->state.dstPitch  = 1024UL;
    batch->state.zorg = 0UL;                    /* the visible framebuffer */
    fillRect(&batch->tri[0], 0UL, BAND, 0x7000UL, DWG_ZI | ZMODE_NOZCMP);
    (void)[master setIntValues:(unsigned *)0 forParameter:SUBMIT_PARAM
                  objectNumber:objNum count:0];
    v = verdict(master, objNum);
    printf("   zorg at the visible framebuffer, atype ZI -> verdict %u (%s)\n",
           v, why(v));
    if (v != OSMGA_HW3D_E_ZORG) fails++;

    batch->tri[0].dwgctl = DWG_I;               /* same bad zorg, unused now */
    v = ([master setIntValues:(unsigned *)0 forParameter:SUBMIT_PARAM
                 objectNumber:objNum count:0] == IO_R_SUCCESS)
        ? OSMGA_HW3D_OK : verdict(master, objNum);
    printf("   the same bad zorg with atype I  -> verdict %u (%s), and it "
           "must be accepted because depth is not addressed\n", v, why(v));
    if (v != OSMGA_HW3D_OK) fails++;

    /*
     * ---- Does atype I compare depth, and does it leave depth alone? ----
     *
     * Matrox's own register decoder calls atype ZI "depth mode with gouraud"
     * and atype I "Gouraud (with depth compare)"
     * (xf86-video-mga-2.0.0/util/stormdwg.c:32 and :35).  Read plainly, I
     * compares and does not write -- which is glDepthMask(GL_FALSE), a state
     * this driver has had to refuse.  Read is all it is, though: no
     * measurement in this tree has ever asked, because every atype I the
     * driver has ever sent carried zmode NOZCMP, and a comparison that always
     * passes cannot be told apart from no comparison at all.
     *
     * This asks.  Two bands, both starting from the same cleared depth:
     *
     *   band 0   I + ZLT, z nearer than the clear   -- must draw
     *   band 1   I + ZLT, z farther than the clear  -- must not draw
     *
     * and afterwards the depth of BOTH must still be the clear.  The three
     * answers are distinguishable and none of them is a guess:
     *
     *   drew, did not draw, depth unchanged  -> I compares and does not write
     *   both drew                            -> I ignores depth
     *   depth changed                        -> I writes, and is not a mask
     *
     * Until the kernel learned that atype I with a real zmode addresses
     * depth, this could not have been asked from here at all: the encoder
     * handed such a batch the scratch depth origin, so the comparison would
     * have been against somebody else's memory.
     */
    for (row = 0UL; row < ROWS; row++)
        for (col = 0UL; col < CLIP_COLS; col++) {
            colour[row * STRIDE_DW + col] = SENTINEL;
            depth[row * STRIDE_DW + col] =
                (row < NBAND * BAND) ? ZCLEAR : ZGUARD;
        }
    if (depth[0] != ZCLEAR) {
        printf("   FAIL -- the second depth clear did not read back (%04x)\n",
               depth[0]);
        return 1;
    }

    memset(batch, 0, sizeof *batch);
    batch->magic = OSMGA_HW3D_MAGIC;
    batch->version = OSMGA_HW3D_VERSION;
    batch->triCount = 2;
    batch->state.dstorg = COLOUR_ORG;
    batch->state.dstWidth  = 64UL;
    batch->state.dstHeight = 120UL;
    batch->state.dstPitch  = 1024UL;
    batch->state.zorg = DEPTH_ORG;
    fillRect(&batch->tri[0], 0UL,  BAND, 0x4000UL, DWG_I | ZMODE_ZLT);
    fillRect(&batch->tri[1], BAND, BAND, 0xC000UL, DWG_I | ZMODE_ZLT);

    v = ([master setIntValues:(unsigned *)0 forParameter:SUBMIT_PARAM
                 objectNumber:objNum count:0] == IO_R_SUCCESS)
        ? OSMGA_HW3D_OK : verdict(master, objNum);
    if (v != OSMGA_HW3D_OK) {
        printf("   atype I with a real zmode was refused (verdict %u, %s)\n",
               v, why(v));
        fails++;
    } else {
        unsigned long drewNear = 0UL, drewFar = 0UL, zmoved = 0UL;

        for (row = 0UL; row < BAND; row++)
            for (col = 0UL; col < CLIP_COLS; col++) {
                if (colour[row * STRIDE_DW + col] != SENTINEL) drewNear++;
                if (depth[row * STRIDE_DW + col] != ZCLEAR) zmoved++;
            }
        for (row = BAND; row < 2UL * BAND; row++)
            for (col = 0UL; col < CLIP_COLS; col++) {
                if (colour[row * STRIDE_DW + col] != SENTINEL) drewFar++;
                if (depth[row * STRIDE_DW + col] != ZCLEAR) zmoved++;
            }

        printf("   atype I + ZLT: near band drew %lu of %lu, far band drew "
               "%lu of %lu, depth moved in %lu pixels\n",
               drewNear, BAND * CLIP_COLS, drewFar, BAND * CLIP_COLS, zmoved);

        /*
         * The far band drew nothing -- but it differs from the near band in
         * its ROWS as well as its depth, so on its own that is two changes
         * and one result.  This is the control: the same rows again, atype I
         * with NOZCMP, which cannot reject anything.  If those rows can be
         * drawn at all, the only thing left to explain the empty band is the
         * comparison.
         */
        memset(batch, 0, sizeof *batch);
        batch->magic = OSMGA_HW3D_MAGIC;
        batch->version = OSMGA_HW3D_VERSION;
        batch->triCount = 1;
        batch->state.dstorg = COLOUR_ORG;
        batch->state.dstWidth  = 64UL;
        batch->state.dstHeight = 120UL;
        batch->state.dstPitch  = 1024UL;
        batch->state.zorg = DEPTH_ORG;
        fillRect(&batch->tri[0], BAND, BAND, 0xC000UL, DWG_I | ZMODE_NOZCMP);
        if ([master setIntValues:(unsigned *)0 forParameter:SUBMIT_PARAM
                    objectNumber:objNum count:0] != IO_R_SUCCESS) {
            printf("   the NOZCMP control over the far band was refused\n");
            fails++;
        } else {
            unsigned long ctrl = 0UL;

            for (row = BAND; row < 2UL * BAND; row++)
                for (col = 0UL; col < CLIP_COLS; col++)
                    if (colour[row * STRIDE_DW + col] != SENTINEL) ctrl++;
            printf("   control: the same rows with NOZCMP drew %lu of %lu\n",
                   ctrl, BAND * CLIP_COLS);
            if (ctrl != BAND * CLIP_COLS) {
                printf("   FAIL -- those rows cannot be drawn at all, so the "
                       "empty far band proves nothing about the comparison\n");
                fails++;
            }
        }

        /*
         * And the guard, recounted.  It was counted before any of this and
         * asserted after it, which meant the assertion was about the earlier
         * batches and said nothing at all about these.
         */
        guardC = 0UL;
        guardZ = 0UL;
        for (row = NBAND * BAND; row < ROWS; row++)
            for (col = 0UL; col < CLIP_COLS; col++) {
                if (colour[row * STRIDE_DW + col] != SENTINEL) guardC++;
                if (depth[row * STRIDE_DW + col] != ZGUARD) guardZ++;
            }
        printf("   guard after the atype I batches: colour %lu, depth %lu\n",
               guardC, guardZ);
        if (drewNear == BAND * CLIP_COLS && drewFar == 0UL && zmoved == 0UL)
            printf("   ANSWER: atype I compares depth and does not write it "
                   "-- glDepthMask(GL_FALSE) is expressible\n");
        else if (drewNear == BAND * CLIP_COLS &&
                 drewFar == BAND * CLIP_COLS && zmoved == 0UL)
            printf("   ANSWER: atype I ignores depth -- the comparison did "
                   "not happen, and a mask cannot be spelled this way\n");
        else if (zmoved != 0UL)
            printf("   ANSWER: atype I WRITES depth -- it is not a mask\n");
        else
            printf("   ANSWER: neither -- read the counts above before "
                   "concluding anything\n");
    }

    /*
     * And the containment that goes with it: a comparing atype I must have
     * its depth origin bounded now, where before nobody looked.
     */
    memset(batch, 0, sizeof *batch);
    batch->magic = OSMGA_HW3D_MAGIC;
    batch->version = OSMGA_HW3D_VERSION;
    batch->triCount = 1;
    batch->state.dstorg = COLOUR_ORG;
    batch->state.dstWidth  = 64UL;
    batch->state.dstHeight = 120UL;
    batch->state.dstPitch  = 1024UL;
    batch->state.zorg = 0UL;                    /* the visible framebuffer */
    fillRect(&batch->tri[0], 0UL, BAND, 0x4000UL, DWG_I | ZMODE_ZLT);
    (void)[master setIntValues:(unsigned *)0 forParameter:SUBMIT_PARAM
                  objectNumber:objNum count:0];
    v = verdict(master, objNum);
    printf("   the same bad zorg with atype I + ZLT -> verdict %u (%s), and "
           "it must be refused because a comparison reads depth\n",
           v, why(v));
    if (v != OSMGA_HW3D_E_ZORG) fails++;

    if (guardC != 0UL || guardZ != 0UL)
        printf("STOP -- a write escaped its band (colour %lu, depth %lu)\n",
               guardC, guardZ);
    else if (drew[0] == 0UL)
        printf("FAIL -- the NOZCMP control drew nothing, so atype ZI does "
               "not draw through this path; ignore the rest\n");
    else if (fails != 0U)
        printf("FAIL -- %u checks disagreed; read the table above\n", fails);
    else
        printf("PASS -- depth compares, updates and rejects through the "
               "batch path, and the validator checks zorg only when it is "
               "addressed\n");
    (void)close(fd);
    return 0;
}
