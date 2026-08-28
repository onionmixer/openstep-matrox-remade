/*
 * W18: WHERE are the two alpha tests?
 *
 * W17 measured that aten changes nothing on an untextured triangle -- which
 * it cannot, since with no texture the first test has nothing to look at.
 * This puts the texture's alpha At and the interpolated alpha Af on
 * opposite sides of the reference and asks whether aten moves the result.
 *
 * THE PIPELINE, from the spec and from this driver:
 *
 *     filtered texture alpha At
 *         -> first alpha test        3-34, "right after texture filtering"
 *         -> TDUALSTAGE alpha  -> Av   TEXF_TEXALPHA picks At or Af HERE
 *         -> ALPHACTRL.alphasel
 *         -> second alpha test       3-34, "before alpha blending"
 *
 * So TEXALPHA changes the STAGE OUTPUT, not what the first test sees, and
 * with alphasel = 1 the second test sees Af either way.  That makes
 * TEXALPHA a two-level experimental factor rather than a control:
 *
 *   A differs at both levels  -> the first test reads filtered At
 *   A differs only when set   -> the first test reads the stage output Av
 *   A differs at neither      -> ambiguous; see the null vocabulary below
 *
 * PART ONE COMES FIRST, and is not optional.  The most plausible way to get
 * a false null here is a texture that never arrived: if At is not really
 * being sampled, "aten changed nothing" is a statement about an absent
 * texture.  So the stage output is read back as a NUMBER, through the
 * framebuffer's alpha byte, with both alpha tests switched off.
 *
 * PER-MODE VECTORS.  One (At, Af) pair cannot serve six comparisons: with
 * At=200 and Af=64 only ALT and ALTE give early-fail with late-pass, and in
 * the other four no value of aten could change anything.  Each mode gets a
 * vector that makes the first test fail and the second pass.
 *
 * ALPHASTART is a signed 9.15 value, so an alpha byte A is A << 15 and the
 * increments are zero -- the alpha must not vary across the band.
 *
 *   cc -O -Wall -I../hw3d -o probe openstep-mga-hw3d-alphastage-probe.m -lDriver
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

static unsigned long osmgaColourOrg, osmgaTexOrg;
#define COLOUR_ORG      osmgaColourOrg
#define TEX_ORG         osmgaTexOrg
#define STRIDE_DW       1024UL
#define DIM             64UL

#define DWG_TEX         (OSMGA_HW3D_OPCODE_TEX | (OSMGA_HW3D_ATYPE_I << 4))
#define SENTINEL        0x11223344UL
#define TEX_RGB         0x00C08040UL

#define AT_EN           0x00001000UL
#define AT_REF(v)       (((unsigned long)(v)) << 16)
#define AT_MODE(m)      (((unsigned long)(m)) << 13)
#define ALPHASEL(s)     (((unsigned long)(s)) << 24)
/* alphamode 01 writes the pixel's alpha to the frame buffer, which is how
 * part one reads the stage output back as a number.  Blending off is src
 * ONE with dst ZERO -- the spec's own way of disabling it. */
#define AC_BASE         (0x1UL | (0x0UL << 4) | 0x100UL)

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
static volatile unsigned long *tex;

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
 * One textured band.  Returns the pixel word, or 0xFFFFFFFF if the batch
 * was refused -- which pixel equality alone could not tell from a discard.
 */
static unsigned long
band(unsigned long At, unsigned long Af, unsigned long ac,
     int texAlpha, unsigned *verdict)
{
    unsigned long step = 1UL << (20UL - 6UL);   /* one texel per pixel */
    OSMGAHW3DTri *t;
    unsigned long r, c, i;

    for (r = 0UL; r < DIM; r++)
        for (c = 0UL; c < DIM; c++)
            tex[r * DIM + c] = (At << 24) | TEX_RGB;
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
    batch->state.texorg = TEX_ORG;
    batch->state.texW = DIM;
    batch->state.texH = DIM;
    batch->state.texPitch = DIM;
    batch->state.texFormat = OSMGA_HW3D_TEXFMT_TW32;
    batch->state.texFlags = texAlpha ? OSMGA_HW3D_TEXF_TEXALPHA : 0UL;
    batch->state.tmr[0] = (long)step;
    batch->state.tmr[3] = (long)step;

    t = &batch->tri[0];
    memset(t, 0, sizeof *t);
    t->dwgctl = DWG_TEX;
    t->alphactrl = ac;
    t->y = 0L;
    t->h = (long)DIM;
    t->ar0 = (long)DIM;
    t->ar6 = (long)DIM;
    t->fxbndry = (DIM << 16) | 0UL;
    t->tq0 = 1L << 16;
    t->a0 = Af << 15;              /* 9.15; adx and ady stay zero */

    *verdict = fire();
    if (*verdict != OSMGA_HW3D_OK)
        return 0xFFFFFFFFUL;
    return colour[2UL * STRIDE_DW + 8UL];
}

/* draw = the band landed, gone = it was discarded, -1 = neither */
static int
drew(unsigned long w)
{
    if (w == 0xFFFFFFFFUL) return -1;
    if (w == SENTINEL)     return 0;
    if ((w & 0x00FFFFFFUL) == TEX_RGB) return 1;
    return -1;
}

int
main(void)
{
    IOString kind;
    caddr_t cmd, cwin, twin;
    int fd, m, ta, en, fails = 0;
    unsigned v;
    static const struct {
        unsigned code; const char *name; unsigned long At, Af;
    } modes[6] = {
        /* each vector: the FIRST test fails, the SECOND passes */
        { AT_E,   "AE  ==",   0UL, 128UL },
        { AT_NE,  "ANE <>", 128UL,   0UL },
        { AT_LT,  "ALT  <", 200UL,  64UL },
        { AT_LTE, "ALTE<=", 200UL,  64UL },
        { AT_GT,  "AGT  >",   0UL, 200UL },
        { AT_GTE, "AGTE>=",   0UL, 128UL }
    };

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
        osmgaTexOrg    = osmgaColourOrg + 2UL * 1024UL * 1024UL;
    }
    if ((fd = open(DEV_PATH, O_RDWR)) < 0) { printf("no %s\n", DEV_PATH); return 1; }
    cmd  = mapDevice(fd, CMD_MMAP_BASE, CMD_MMAP_LEN);
    cwin = mapDevice(fd, COLOUR_ORG, (int)(DIM * STRIDE_DW * 4UL));
    twin = mapDevice(fd, TEX_ORG, (int)(DIM * DIM * 4UL));
    if (cmd == (caddr_t)-1 || cwin == (caddr_t)-1 || twin == (caddr_t)-1) {
        printf("a window will not map\n"); return 1;
    }
    batch  = (OSMGAHW3DBatch *)cmd;
    colour = (volatile unsigned long *)cwin;
    tex    = (volatile unsigned long *)twin;

    /* ---- part one: is the texture actually being sampled? -------------- */
    printf("part one -- the stage output, read back as a number\n");
    printf("  both alpha tests off, alphasel 0, so the pixel alpha IS Av\n");
    for (ta = 0; ta <= 1; ta++) {
        unsigned long w = band(200UL, 64UL,
                               AC_BASE | ALPHASEL(0) | AT_MODE(AT_ALWAYS)
                                       | AT_REF(128),
                               ta, &v);
        unsigned long got = (w == 0xFFFFFFFFUL) ? 0xFFFFFFFFUL : (w >> 24);
        unsigned long want = ta ? 200UL : 64UL;

        printf("  TEXALPHA %d: word %08lx, alpha %lu, wanted %lu (Av = %s)%s\n",
               ta, w, got, want, ta ? "At" : "Af",
               (got == want) ? "" : "   <<< the stage is not doing this");
        if (got != want) fails++;
    }
    if (fails) {
        printf("\nSTOP -- the stage output is not what it should be, so any\n"
               "result below would be about something other than the texture\n");
        return 1;
    }

    /* ---- part two: does aten move the result? -------------------------- */
    printf("\npart two -- alphasel 1, so the SECOND test sees Af\n");
    printf("  vectors chosen so the FIRST test fails and the SECOND passes\n");
    printf("  texa mode    At  Af  aten=1 aten=0  aten matters?\n");
    for (ta = 1; ta >= 0; ta--) {
        int diffs = 0;

        for (m = 0; m < 6; m++) {
            int r[2];

            for (en = 0; en <= 1; en++) {
                unsigned long ac = AC_BASE | ALPHASEL(1)
                                 | (en ? AT_EN : 0UL)
                                 | AT_MODE(modes[m].code) | AT_REF(128);
                r[en] = drew(band(modes[m].At, modes[m].Af, ac, ta, &v));
                if (r[en] < 0) {
                    printf("  refused or unreadable (verdict %u)\n", v);
                    fails++;
                }
            }
            printf("  %4d %-7s %3lu %3lu  %6s %6s  %s\n",
                   ta, modes[m].name, modes[m].At, modes[m].Af,
                   r[1] ? "draw" : "gone", r[0] ? "draw" : "gone",
                   (r[0] != r[1]) ? "YES" : "no");
            if (r[0] != r[1]) diffs++;
        }
        printf("  -> TEXALPHA %d: aten changed the result in %d of 6 modes\n\n",
               ta, diffs);
    }

    printf("reading the table:\n"
           "  differs at BOTH TEXALPHA levels -> the first test reads filtered At\n"
           "  differs only when TEXALPHA is 1 -> it reads the stage output Av\n"
           "  differs at NEITHER              -> no aten-visible early rejection\n"
           "                                     in this single-texture TW32\n"
           "                                     configuration.  Not 'anywhere'.\n");
    printf("\n%s (%d problems)\n",
           fails ? "SOMETHING WAS REFUSED OR UNREADABLE" : "every band drew or was discarded cleanly",
           fails);
    return fails ? 1 : 0;
}
