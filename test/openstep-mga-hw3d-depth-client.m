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
 * state.dwgctl is per batch, not per triangle, so each band needs its own
 * submission.  That is not a workaround: back-to-back submissions were on
 * the unproven list, so four of them is the cheaper of two tests.
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
#define CMD_MMAP_LEN    (64 * 1024)
#define COLOUR_ORG      (4UL * 1024UL * 1024UL)
#define DEPTH_ORG       (5UL * 1024UL * 1024UL)
#define STRIDE_DW       1024UL              /* 1024x768x4 */
#define CLIP_COLS       64UL
#define BAND            20UL
#define NBAND           4UL
#define ROWS            ((NBAND + 1UL) * BAND)      /* + a guard band */
#define SENTINEL        0x5A5A5A5AUL
#define ZCLEAR          0x8000U
#define ZGUARD          0xC0DEU

/* atype I is 7 << 4, ZI is 3 << 4; zmode lives in bits 8-10. */
#define DWG_I           0x000C7074UL
#define DWG_ZI          ((DWG_I & ~0x70UL) | 0x30UL)
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
fillRect(OSMGAHW3DTri *t, unsigned long y, unsigned long h, unsigned long z)
{
    memset(t, 0, sizeof *t);
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

    printf("M1-2e: depth through the batch path\n");

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

    for (b = 0UL; b < NBAND; b++) {
        IOReturn r;

        memset(batch, 0, sizeof *batch);
        batch->magic = OSMGA_HW3D_MAGIC;
        batch->version = OSMGA_HW3D_VERSION;
        batch->triCount = 1;
        batch->state.dstorg = COLOUR_ORG;
        batch->state.zorg = DEPTH_ORG;
        batch->state.dwgctl = DWG_ZI | zmode[b];
        batch->state.alphactrl = 0x00000101UL;
        fillRect(&batch->tri[0], b * BAND, BAND, zval[b]);

        r = [master setIntValues:(unsigned *)0 forParameter:SUBMIT_PARAM
                    objectNumber:objNum count:0];
        if (r != IO_R_SUCCESS) {
            printf("   band %lu was refused (%d, verdict %u %s)\n",
                   b, (int)r, verdict(master, objNum),
                   why(verdict(master, objNum)));
            return 1;
        }

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

    /* The validator, both ways round. */
    memset(batch, 0, sizeof *batch);
    batch->magic = OSMGA_HW3D_MAGIC;
    batch->version = OSMGA_HW3D_VERSION;
    batch->triCount = 1;
    batch->state.dstorg = COLOUR_ORG;
    batch->state.zorg = 0UL;                    /* the visible framebuffer */
    batch->state.dwgctl = DWG_ZI | ZMODE_NOZCMP;
    batch->state.alphactrl = 0x00000101UL;
    fillRect(&batch->tri[0], 0UL, BAND, 0x7000UL);
    (void)[master setIntValues:(unsigned *)0 forParameter:SUBMIT_PARAM
                  objectNumber:objNum count:0];
    v = verdict(master, objNum);
    printf("   zorg at the visible framebuffer, atype ZI -> verdict %u (%s)\n",
           v, why(v));
    if (v != OSMGA_HW3D_E_ZORG) fails++;

    batch->state.dwgctl = DWG_I;                /* same bad zorg, unused now */
    v = ([master setIntValues:(unsigned *)0 forParameter:SUBMIT_PARAM
                 objectNumber:objNum count:0] == IO_R_SUCCESS)
        ? OSMGA_HW3D_OK : verdict(master, objNum);
    printf("   the same bad zorg with atype I  -> verdict %u (%s), and it "
           "must be accepted because depth is not addressed\n", v, why(v));
    if (v != OSMGA_HW3D_OK) fails++;

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
