/*
 * openstep-mga-hw3d-blendsat-client.m -- does source factor 8 carry GL's split?
 *
 * GL_SRC_ALPHA_SATURATE is not one factor, it is two.  GL asks for
 * min(As, 1 - Ad) on the colour channels and exactly ONE on alpha -- Mesa's
 * own blend does precisely that (Mesa-3.4.2/src/blend.c:550-557 for colour,
 * :616-618 for alpha, where sA is set to 1.0 and nothing else).  The engine
 * has a single source-factor field for all four channels, and Matrox's
 * register list does name the encoding: AC_src_src_alpha_sat = 0x8, and only
 * as a SOURCE factor, the destination list stopping at seven
 * (xf86-video-mga-2.0.0/src/mgareg_flags.h:50 and :52-59), which is GL's rule.
 *
 * So the encoding exists and the validator already admits it.  What nobody
 * has measured is whether the hardware carries the split, and until that is
 * measured the back end refuses the state.  This measures it.
 *
 * THREE HYPOTHESES, and the alpha channel separates all three:
 *
 *   split      the GL one -- f on colour, one on alpha
 *   one-factor f on all four, alpha included
 *   plain      factor 8 is really factor 4, and the name is a lie
 *   halfsplit  source alpha on colour, ONE on alpha -- the minimum never
 *              taken, but the alpha channel still special
 *
 * THE ANSWER IS THE FOURTH, on all five rows and to the level: the engine
 * puts source alpha on the colour channels and exactly one on alpha, and
 * 255 - Ad never enters.  It is not a destination-alpha blindness either --
 * the factor 7 control reads Ad correctly on every row, Ad = 200 included.
 *
 * So this is NOT GL_SRC_ALPHA_SATURATE, which needs min(As, 1 - Ad) on the
 * colour.  It is the source side of glBlendFuncSeparate(SRC_ALPHA, ONE),
 * which GL 1.1 has no way to ask for.  The back end goes on refusing the
 * state, now because of this rather than because nobody had looked.
 *
 * The destination factor is ZERO throughout so the destination contributes
 * nothing and the source factor is the only thing being read.  The rows are
 * chosen so that f actually BITES: with As below 255 - Ad the minimum is As
 * and factor 8 collapses into factor 4, which is why section 86's numbers --
 * destination alpha nought, so 255 - Ad is 255 -- could never have answered
 * this even if it had looked at alpha, which it does not.
 *
 *   cc -O -Wall -I../hw3d -o tsa openstep-mga-hw3d-blendsat-client.m -lDriver
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
static unsigned long osmgaTexOrg;
#define TEX_ORG         osmgaTexOrg
#define STRIDE_DW       1024UL
#define DIM             64UL
#define ROWS            64UL

/* TRAP with a texture, access type I: no depth anywhere in this. */
#define DWG_TEX         (0x6UL | (0x7UL << 4))

/*
 * src factor 8, dst factor ZERO, amode 1 (the alpha channel rather than
 * FCOL), alphasel 0 (the value comes from the texture stage -- which is the
 * stage section 87 showed the alpha test reads, and section 81 the blend).
 */
#define AC_SAT_ZERO     (0x8UL | (0x0UL << 4) | 0x100UL)
#define AC_SRCA_ZERO    (0x4UL | (0x0UL << 4) | 0x100UL)
/* one minus DESTINATION alpha as the SOURCE factor: the control that says
 * whether the engine reads the destination alpha this probe wrote at all */
#define AC_OMDA_ZERO    (0x7UL | (0x0UL << 4) | 0x100UL)

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
 * One textured band, drawn over a destination this fills first.
 *
 * The texel alpha is As and the destination alpha is Ad; TEXALPHA is what
 * makes the stage alpha the TEXEL's rather than the interpolated one, and
 * the interpolated alpha is deliberately set to something else so that a
 * path taking the wrong one cannot pass by coincidence.
 */
static unsigned long
draw(unsigned long As, unsigned long Ad, unsigned long ac, unsigned *verdict)
{
    unsigned long r, c;
    OSMGAHW3DTri *t;
    unsigned long step = 1UL << (20UL - 6UL);

    for (r = 0UL; r < DIM; r++)
        for (c = 0UL; c < DIM; c++)
            tex[r * DIM + c] = (As << 24) | 0x00C08040UL;
    for (r = 0UL; r < ROWS; r++)
        for (c = 0UL; c < STRIDE_DW; c++)
            colour[r * STRIDE_DW + c] = (Ad << 24) | 0x00804020UL;

    memset(batch, 0, sizeof *batch);
    batch->magic = OSMGA_HW3D_MAGIC;
    batch->version = OSMGA_HW3D_VERSION;
    batch->triCount = 1;
    batch->state.dstorg = COLOUR_ORG;
    batch->state.dstWidth  = DIM;
    batch->state.dstHeight = ROWS;
    batch->state.dstPitch  = STRIDE_DW;
    batch->state.texorg = TEX_ORG;
    batch->state.texW = DIM;
    batch->state.texH = DIM;
    batch->state.texPitch = DIM;
    batch->state.texFormat = OSMGA_HW3D_TEXFMT_TW32;
    batch->state.texFlags = OSMGA_HW3D_TEXF_TEXALPHA;
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
    /* not As, on purpose: TEXALPHA must be what decides */
    t->a0 = 0x10UL << 15;

    *verdict = fire();
    if (*verdict != OSMGA_HW3D_OK)
        return 0xFFFFFFFFUL;
    return colour[2UL * STRIDE_DW + 8UL];
}

/* the engine's arithmetic, fitted over 262144 samples: multiply twice, add,
 * round ONCE, and saturate rather than wrap */
static unsigned long
eng(unsigned long Cs, unsigned long Fs, unsigned long Cd, unsigned long Fd)
{
    unsigned long v = (Cs * Fs + Cd * Fd + 127UL) / 255UL;

    return (v > 255UL) ? 255UL : v;
}

int
main(void)
{
    IOString kind;
    caddr_t cmd, cwin, twin;
    int fd, i;
    unsigned fails = 0U;
    static const struct { unsigned long as, ad; const char *note; } rows[5] = {
        {  64UL,  64UL, "As below 255-Ad: f is As, so 8 and 4 agree"      },
        { 128UL, 127UL, "As equals 255-Ad, the boundary"                  },
        { 192UL, 128UL, "As ABOVE 255-Ad: f is 127, and all three differ" },
        { 240UL, 200UL, "further above: f is 55, and they differ more"    },
        { 128UL,   0UL, "Ad nought -- the case section 86 had, inert"     }
    };

    master = [IODeviceMaster new];
    if ([master lookUpByDeviceName:"Display0" objectNumber:&objNum
            deviceKind:&kind] != IO_R_SUCCESS) {
        printf("Display0 not found\n"); return 1;
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
        /* Two MiB above colour, clear of it, and 32-aligned as TEXORG
         * requires because the window base is. */
        osmgaTexOrg = osmgaColourOrg + 2UL * 1024UL * 1024UL;
    }

    if ((fd = open(DEV_PATH, O_RDWR)) < 0) {
        printf("%s will not open\n", DEV_PATH); return 1;
    }
    cmd  = mapDevice(fd, CMD_MMAP_BASE, CMD_MMAP_LEN);
    cwin = mapDevice(fd, COLOUR_ORG, (int)(ROWS * STRIDE_DW * 4UL));
    twin = mapDevice(fd, TEX_ORG, (int)(DIM * DIM * 4UL));
    if (cmd == (caddr_t)-1 || cwin == (caddr_t)-1 || twin == (caddr_t)-1) {
        printf("a window will not map\n"); return 1;
    }
    batch  = (OSMGAHW3DBatch *)cmd;
    colour = (volatile unsigned long *)cwin;
    tex    = (volatile unsigned long *)twin;

    printf("source factor 8: does it carry GL's split?\n\n");
    printf("   texel C08040, destination 804020, destination factor ZERO\n\n");
    printf("   %4s %4s %4s   %-8s   %-8s %-8s %-8s %-8s\n",
           "As", "Ad", "f", "engine", "half", "split", "onefactor", "plain");

    for (i = 0; i < 5; i++) {
        unsigned long As = rows[i].as, Ad = rows[i].ad;
        unsigned long f = (As < 255UL - Ad) ? As : (255UL - Ad);
        unsigned long got, ctrl;
        unsigned long split, one, plain, half;
        unsigned v, vc;
        const char *verdictName;

        got = draw(As, Ad, AC_SAT_ZERO, &v);
        if (v != OSMGA_HW3D_OK) {
            printf("   %4lu %4lu %4lu   REFUSED (verdict %u)\n", As, Ad, f, v);
            fails++;
            continue;
        }

        split = (eng(As, 255UL, Ad, 0UL) << 24)
              | (eng(0xC0UL, f, 0x80UL, 0UL) << 16)
              | (eng(0x80UL, f, 0x40UL, 0UL) <<  8)
              |  eng(0x40UL, f, 0x20UL, 0UL);
        one   = (eng(As, f, Ad, 0UL) << 24)
              | (eng(0xC0UL, f, 0x80UL, 0UL) << 16)
              | (eng(0x80UL, f, 0x40UL, 0UL) <<  8)
              |  eng(0x40UL, f, 0x20UL, 0UL);
        plain = (eng(As, As, Ad, 0UL) << 24)
              | (eng(0xC0UL, As, 0x80UL, 0UL) << 16)
              | (eng(0x80UL, As, 0x40UL, 0UL) <<  8)
              |  eng(0x40UL, As, 0x20UL, 0UL);

        half  = (eng(As, 255UL, Ad, 0UL) << 24)
              | (eng(0xC0UL, As, 0x80UL, 0UL) << 16)
              | (eng(0x80UL, As, 0x40UL, 0UL) <<  8)
              |  eng(0x40UL, As, 0x20UL, 0UL);

        verdictName = (got == half)  ? "src alpha on colour, ONE on alpha"
                    : (got == split) ? "SPLIT (GL)"
                    : (got == one)   ? "one factor"
                    : (got == plain) ? "plain src alpha"
                                     : "NONE OF THEM";
        printf("   %4lu %4lu %4lu   %08lx   %08lx %08lx %08lx %08lx   %s\n",
               As, Ad, f, got, half, split, one, plain, verdictName);
        /*
         * The measured answer is the fourth, so anything else is the finding
         * having moved -- which is a failure of this test's premise and has
         * to be loud.  Rows where f happens to equal As cannot tell the
         * fourth from the GL one on colour; the rows where it bites can, and
         * they are the ones that decide.
         */
        if (got != half) {
            printf("        FAIL  this row no longer reads as source alpha"
                   " on colour with one on alpha\n");
            fails++;
        }

        /*
         * Two controls, because "the engine did not take the minimum" and
         * "the engine could not see the destination alpha" produce the same
         * numbers: with Ad read as nought, 255 - Ad is 255 and min(As, 255)
         * is As, which is what plain source alpha gives.
         *
         *   factor 4  -- source alpha, so the colour side of the comparison
         *   factor 7  -- ONE MINUS DESTINATION ALPHA, which is nothing BUT
         *                the destination alpha, and pins whether the engine
         *                reads the byte this probe wrote
         */
        ctrl = draw(As, Ad, AC_SRCA_ZERO, &vc);
        if (vc != OSMGA_HW3D_OK) {
            printf("        the factor 4 control was refused (%u)\n", vc);
            fails++;
        } else
            printf("        factor 4 (source alpha):        %08lx\n", ctrl);

        {
            unsigned long da, want;
            unsigned vd;

            da = draw(As, Ad, AC_OMDA_ZERO, &vd);
            want = (eng(As, 255UL - Ad, Ad, 0UL) << 24)
                 | (eng(0xC0UL, 255UL - Ad, 0x80UL, 0UL) << 16)
                 | (eng(0x80UL, 255UL - Ad, 0x40UL, 0UL) <<  8)
                 |  eng(0x40UL, 255UL - Ad, 0x20UL, 0UL);
            if (vd != OSMGA_HW3D_OK) {
                printf("        the factor 7 control was refused (%u)\n", vd);
                fails++;
            } else {
                printf("        factor 7 (1 - dest alpha):     %08lx  "
                       "python %08lx  %s\n", da, want,
                       (da == want) ? "the engine DOES read Ad"
                                    : "<-- differs, so Ad is not what it reads");
                if (da != want) fails++;
            }
        }
    }

    printf("\n%s\n", fails
           ? "=== PROBLEM: read the table ==="
           : "=== source factor 8 is source alpha on colour and ONE on"
             " alpha, so it is not GL_SRC_ALPHA_SATURATE ===");
    return fails ? 1 : 0;
}
