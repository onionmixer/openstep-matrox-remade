/*
 * W17: there are two alpha tests, and this asks which one is working.
 *
 * The register says so (3-34): "Alpha testing is done in two separate
 * places.  Both use the same reference value (atRef) and alpha test mode
 * (atMode).  The first test is done right after texture filtering and can
 * be disabled by setting the 'atEn' bit to '0', or by setting 'atmode' to
 * ALWAYS.  The second test is done before alpha blending and can be
 * disabled by programming 'atmode' to ALWAYS."
 *
 * That matters because section 4.5.5.4 requires aten = 0 for a Gouraud
 * trapezoid, and tat measures the untextured alpha test working with
 * aten = 1.  If the late test is what tat sees, the two statements do not
 * conflict and Mesa could send the spec-compliant bit.
 *
 * WHY A RAW CLIENT.  Mesa cannot express the case: it sets aten whenever
 * alpha testing is enabled, before it looks at texturing.
 *
 * WHAT THIS DOES NOT DO.  It draws no texture, so it cannot separate the
 * two tests by POSITION -- that needs texture alpha and final alpha on
 * opposite sides of the reference, which is a second probe.  What it can
 * establish is whether an atmode-driven comparison survives aten = 0.
 *
 * The alphas are stated rather than converted: ALPHASTART is a signed 9.15
 * value, so an alpha byte A is A << 15 and its integer part is A.  atref is
 * eight bits at 23:16.  Reference 128, alphas 127 / 128 / 129.
 *
 *   cc -O -Wall -I../hw3d -o probe openstep-mga-hw3d-alphatest-probe.m -lDriver
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

static unsigned long osmgaColourOrg;
#define COLOUR_ORG      osmgaColourOrg
#define STRIDE_DW       1024UL
#define DIM             64UL

/* Gouraud trapezoid, no texture: opcode TRAP with atype I. */
#define DWG_TRAP        (OSMGA_HW3D_OPCODE_TRAP | (OSMGA_HW3D_ATYPE_I << 4))

#define SENTINEL        0x11223344UL
#define DRAWN_R         200UL
#define DRAWN_G         100UL
#define DRAWN_B          50UL

#define AT_EN           0x00001000UL
#define AT_REF(v)       (((unsigned long)(v)) << 16)
#define AT_MODE(m)      (((unsigned long)(m)) << 13)
#define ALPHASEL(s)     (((unsigned long)(s)) << 24)
/* alphamode 01 = the alpha channel; blending off is src ONE, dst ZERO. */
#define AC_BASE         (0x1UL | (0x0UL << 4) | 0x100UL)

/* atmode encodings, 3-34.  1 is reserved and never sent. */
#define AT_ALWAYS 0U
#define AT_E      2U
#define AT_NE     3U
#define AT_LT     4U
#define AT_LTE    5U
#define AT_GT     6U
#define AT_GTE    7U

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
fire(void)
{
    unsigned st[4], n = 4;

    if ([master setIntValues:(unsigned *)0 forParameter:SUBMIT_PARAM
                objectNumber:objNum count:0] == IO_R_SUCCESS)
        return OSMGA_HW3D_OK;
    if ([master getIntValues:st forParameter:STATUS_PARAM objectNumber:objNum
            count:&n] != IO_R_SUCCESS)
        return 0xFFFFU;
    return st[0];
}

/*
 * Draw one band with the given ALPHACTRL and alpha, over a sentinel.
 * Returns 1 if the pixel survived, 0 if it was discarded, -1 if the batch
 * was refused or the pixel is neither -- which pixel equality alone could
 * not tell apart from a discard.
 */
static int
band(unsigned long ac, unsigned long alphaByte, unsigned *verdict)
{
    OSMGAHW3DTri *t;
    unsigned long i, got;

    for (i = 0UL; i < DIM * STRIDE_DW; i++)
        colour[i] = SENTINEL;

    memset(batch, 0, sizeof *batch);
    batch->magic = OSMGA_HW3D_MAGIC;
    batch->version = OSMGA_HW3D_VERSION;
    batch->triCount = 1UL;
    batch->state.dstorg = COLOUR_ORG;
    batch->state.dstWidth = DIM;
    batch->state.dstHeight = DIM;
    batch->state.dstPitch = STRIDE_DW;

    t = &batch->tri[0];
    memset(t, 0, sizeof *t);
    t->dwgctl = DWG_TRAP;
    t->alphactrl = ac;
    t->y = 0L;
    t->h = (long)DIM;
    t->ar0 = (long)DIM;
    t->ar6 = (long)DIM;
    t->fxbndry = (DIM << 16) | 0UL;
    t->dr[0] = DRAWN_R << 15;      /* flat colour, no gradient */
    t->dr[3] = DRAWN_G << 15;
    t->dr[6] = DRAWN_B << 15;
    t->a0 = alphaByte << 15;       /* 9.15: integer part is the byte */

    *verdict = fire();
    if (*verdict != OSMGA_HW3D_OK)
        return -1;

    got = colour[2UL * STRIDE_DW + 8UL];
    if (got == SENTINEL)
        return 0;
    if ((got & 0xFFUL) == DRAWN_B)
        return 1;
    return -1;
}

int
main(void)
{
    IOString kind;
    caddr_t cmd, cwin;
    int fd, sel, en, m, k, fails = 0;
    static const struct { unsigned code; const char *name; } modes[6] = {
        { AT_E, "AE  ==" }, { AT_NE, "ANE <>" }, { AT_LT,  "ALT  <" },
        { AT_LTE,"ALTE<=" }, { AT_GT, "AGT  >" }, { AT_GTE, "AGTE>=" }
    };
    static const unsigned long alphas[3] = { 127UL, 128UL, 129UL };

    master = [IODeviceMaster new];
    if ([master lookUpByDeviceName:"Display0" objectNumber:&objNum
            deviceKind:&kind] != IO_R_SUCCESS) {
        printf("Display0 not found\n"); return 1;
    }
    {
        unsigned caps[OSMGA_HW3D_CAPS_COUNT];
        unsigned ncaps = OSMGA_HW3D_CAPS_COUNT;

        if ([master getIntValues:caps forParameter:OSMGA_HW3D_CAPS_PARAM
                    objectNumber:objNum count:&ncaps] != IO_R_SUCCESS ||
            ncaps != OSMGA_HW3D_CAPS_COUNT) {
            printf("capabilities unavailable\n"); return 1;
        }
        osmgaColourOrg = (unsigned long)caps[OSMGA_HW3D_CAP_VRAMOFF];
    }
    if ((fd = open(DEV_PATH, O_RDWR)) < 0) { printf("no %s\n", DEV_PATH); return 1; }
    cmd  = mapDevice(fd, CMD_MMAP_BASE, CMD_MMAP_LEN);
    cwin = mapDevice(fd, COLOUR_ORG, (int)(DIM * STRIDE_DW * 4UL));
    if (cmd == (caddr_t)-1 || cwin == (caddr_t)-1) {
        printf("a window will not map\n"); return 1;
    }
    batch  = (OSMGAHW3DBatch *)cmd;
    colour = (volatile unsigned long *)cwin;

    printf("reference 128, alphas 127/128/129, ALPHASTART = byte << 15\n\n");

    /* The draw baseline.  atmode ALWAYS disables BOTH tests, so this says
     * only that the geometry draws and can be read back -- it does not
     * separate "the late test passed everything" from "nothing ran". */
    printf("draw baseline -- atmode ALWAYS, both tests off\n");
    for (sel = 0; sel <= 1; sel++) {
        unsigned v;
        int r = band(AC_BASE | ALPHASEL(sel) | AT_MODE(AT_ALWAYS) | AT_REF(128),
                     127UL, &v);
        printf("  alphasel %d: %s (verdict %u)\n", sel,
               (r == 1) ? "drew" : (r == 0) ? "DISCARDED -- unexpected"
                                            : "refused or unreadable", v);
        if (r != 1) fails++;
    }

    printf("\nthe sweep -- does an atmode comparison survive aten = 0?\n");
    printf("  sel en mode    a=127 a=128 a=129   expected\n");
    for (sel = 0; sel <= 1; sel++) {
        for (en = 0; en <= 1; en++) {
            for (m = 0; m < 6; m++) {
                int got[3];
                int want[3];
                unsigned v[3];
                int agree = 1;

                for (k = 0; k < 3; k++) {
                    unsigned long a = alphas[k];
                    unsigned long ac = AC_BASE | ALPHASEL(sel)
                                     | AT_EN * (unsigned long)en
                                     | AT_MODE(modes[m].code) | AT_REF(128);
                    got[k] = band(ac, a, &v[k]);
                    switch (modes[m].code) {
                    case AT_E:   want[k] = (a == 128UL); break;
                    case AT_NE:  want[k] = (a != 128UL); break;
                    case AT_LT:  want[k] = (a <  128UL); break;
                    case AT_LTE: want[k] = (a <= 128UL); break;
                    case AT_GT:  want[k] = (a >  128UL); break;
                    default:     want[k] = (a >= 128UL); break;
                    }
                    if (got[k] != want[k]) agree = 0;
                }
                printf("  %3d %2d %-7s %5s %5s %5s   %s%s\n",
                       sel, en, modes[m].name,
                       (got[0] < 0) ? "??" : (got[0] ? "draw" : "gone"),
                       (got[1] < 0) ? "??" : (got[1] ? "draw" : "gone"),
                       (got[2] < 0) ? "??" : (got[2] ? "draw" : "gone"),
                       want[0] ? "draw " : "gone ",
                       agree ? "" : "   <<< DISAGREES");
                if (!agree) fails++;
            }
        }
    }

    printf("\n%s (%d rows disagreeing)\n",
           fails ? "THE TWO SETTINGS DO NOT BEHAVE ALIKE"
                 : "every row matched the comparison it asked for",
           fails);
    return fails ? 1 : 0;
}
