/*
 * R6 replacement display driver for Matrox MGA G450 (primary head).
 *
 * enterLinearMode programs a complete X.Org-derived G450 linear "power
 * graphic" mode directly (no mode-validation/transaction layer -- an earlier
 * design had one, but its exact-60Hz timing check rejected real VESA modes;
 * the original MatroxMGA has none either): generic VGA (Misc/SEQ/CRTC incl.
 * 0x17 reset-release/GR/Attribute+PAS video-on), the RAMDAC pixel-format
 * registers per selected depth (MUL_CTL, PAN_CTL loop filter, palette), and
 * the G450 pixel PLL via a faithful port of the original's sorted-candidate +
 * jitter-band stability search (see docs/R6_G450_PIXEL_PLL_ALGORITHM.md).
 * All register values are the X.Org-verified constants in
 * docs/R6_G450_FULL_LINEAR_MODESET.md.
 *
 * init maps BAR0 (framebuffer, mapMemoryRange range 0) and BAR1 (MMIO, via
 * IOMapPhysicalIntoIOTask on the PCI-config BAR1 address), selects a
 * resolution+pixel-format from the config-table "Display Mode" string (see
 * osmgaRes/osmgaFmt and Display.modes for the Configure.app-visible list),
 * publishes the matching IODisplayInfo, and returns self.  Risky
 * mode-register writes happen in enterLinearMode (after the window server
 * starts, i.e. after the network is up), so a mode failure loses the screen
 * but not telnet; recovery is the R5-VGA config-edit reboot.  Owns the
 * display only when selected in System.config "Active Drivers" and
 * cold-rebooted.
 */

#import <driverkit/generalFuncs.h>
#import <driverkit/kernelDriver.h>
#import <driverkit/IODeviceDescription.h>
#import <driverkit/IOConfigTable.h>
#import <driverkit/i386/ioPorts.h>
#import "OpenStepMGAReplacementDisplay.h"
#import "OpenStepMGAManualConfig.h"
#import "OpenStepMGAEDID.h"

#define PCI_CFG_ADDR            0x0CF8
#define PCI_CFG_DATA            0x0CFC
#define PCI_MAX_BUS             8
#define PCI_MAX_DEVICE          32
#define MGA_VENDOR_ID           0x102B
#define MGA_G400_G450_ID        0x0525
#define MGA_G450_MIN_REVISION   0x80

/* MMIO register offsets (docs/R6_G450_FULL_LINEAR_MODESET.md) */
#define MGA_MISC_READ           0x1fcc
#define MGA_MISC_WRITE          0x1fc2
#define MGA_SEQ_INDEX           0x1fc4
#define MGA_SEQ_DATA            0x1fc5
#define MGA_GR_INDEX            0x1fce
#define MGA_GR_DATA             0x1fcf
#define MGA_ATTR_INDEX          0x1fc0
#define MGA_ATTR_DATA_R         0x1fc1
#define MGA_INSTS1              0x1fda
#define MGA_CRTC_INDEX          0x1fd4
#define MGA_CRTC_DATA           0x1fd5
#define MGA_CRTCEXT_INDEX       0x1fde
#define MGA_CRTCEXT_DATA        0x1fdf
#define MGA_DAC_INDEX           0x3c00
#define MGA_DAC_DATA            0x3c0a
#define MGA_DAC_PIX_M           0x4c
#define MGA_DAC_PIX_N           0x4d
#define MGA_DAC_PIX_P           0x4e
#define MGA_DAC_PIX_STAT        0x4f
#define MGA_DAC_PAN_CTL         0xa2
#define MGA_CLKSEL_MGA          0x0c
#define MGA_PLLLOCK             0x40
#define MGA_MMIO_LENGTH         0x4000
#define MGA_VRAM_16MB           (16UL * 1024UL * 1024UL)
#define MGA_BPP32_SHIFT         2      /* BppShift for 32 bpp */

/*
 * ---- Storm 2D drawing engine (S1 liveness test only) ----
 *
 * Offsets and field meanings are behaviour-level facts derived from the public
 * X.Org xf86-video-mga 2.0.0 driver; no source is copied.  Plan, safety
 * analysis and the codex cross-review are in
 * docs/S1_STORM_ENGINE_LIVENESS_PLAN.md.  These registers are touched ONLY by
 * the opt-in offscreen liveness test; normal boots never write them.
 */
#define MGA_DWGCTL              0x1c00
#define MGA_MACCESS             0x1c04
#define MGA_PLNWT               0x1c1c
#define MGA_BCOL                0x1c20
#define MGA_FCOL                0x1c24
#define MGA_CXBNDRY             0x1c80
#define MGA_FXBNDRY             0x1c84
#define MGA_YDSTLEN             0x1c88
#define MGA_PITCH               0x1c8c
#define MGA_YDSTORG             0x1c94
#define MGA_YTOP                0x1c98
#define MGA_YBOT                0x1c9c
#define MGA_FIFOSTATUS          0x1e10
#define MGA_ENGSTATUS           0x1e14
#define MGA_OPMODE              0x1e54
#define MGA_SRCORG              0x2cb4
#define MGA_DSTORG              0x2cb8
#define MGA_EXEC                0x0100  /* added to a register: write + execute */

#define MGA_ENGBUSY_BIT         0x01    /* ENGSTATUS+2, bit 0 */
#define MGA_OPMODE_DMA_BLIT     0x04
#define MGA_OPMODE_BYTESWAP     0x30000 /* cleared on little-endian */
#define MGA_MACCESS_PW32        0x02

/*
 * Solid rectangle fill, replace mode, ROP=copy, block mode deliberately unused:
 *   TRAP(0x04) | SOLID(1<<11) | ARZERO(1<<12) | SGNZERO(1<<13)
 *   | SHIFTZERO(1<<14) | BMONOLEF(0) | RPL(0<<4) | bop_copy(0x000C0000)
 * ARZERO/SGNZERO remove any dependency on the AR/SGN registers.
 */
#define MGA_DWGCTL_SOLID_FILL   0x000C7804UL

/* ---- S2: screen-to-screen BITBLT ---- */
#define MGA_SGN                 0x1c58
#define MGA_AR0                 0x1c60  /* source span end   */
#define MGA_AR3                 0x1c6c  /* source span start */
#define MGA_AR5                 0x1c74  /* source row delta (signed, pixels) */

/*
 * Screen-to-screen copy, ROP=copy:
 *   AtypeNoBLK[GXcopy] (= RPL | 0x000C0000) | SHIFTZERO(1<<14)
 *   | BITBLT(0x08) | BFCOL(0x02<<25)
 * RPL (not RSTR) is correct for GXcopy: the RPL slots in the table are exactly
 * the ROPs that need no destination read (clear/copy/copyInverted/set).  The
 * same expression appears twice in X.Org (blit setup, and the DWGCTL restore
 * at the end of the fast-blit path).  See docs/S2_STORM_BITBLT_PLAN.md 8-1.
 */
#define MGA_DWGCTL_BITBLT       0x040C4008UL
#define MGA_SGN_DOWN_RIGHT      0x00000000UL
#define MGA_SGN_BLIT_LEFT       0x00000001UL  /* copy right-to-left */
#define MGA_SGN_BLIT_UP         0x00000004UL  /* copy bottom-to-top */

/* ---- S3b-prep: telemetry + test-only blit parameter ---- */
#define OSMGA_STATS_PARAM       "OSMGAStats"
#define OSMGA_STATS_VERSION     1
#define OSMGA_STATS_COUNT       26
/*
 * displayDefs.h says callers must not use IO_DISPLAY_DO_BLIT unless
 * IO_DISPLAY_CAN_BLIT is set.  We have not set it, so our own probe client
 * uses this private parameter instead of violating that contract.  Same six
 * arguments, same validation, same engine helper.
 */
#define OSMGA_PROBE_BLIT_PARAM  "OSMGAProbeBlit"

/* Single primary outcome per request, for the statistics. */
#define OSMGA_BLIT_R_OK         0
#define OSMGA_BLIT_R_NOOP       1
#define OSMGA_BLIT_R_DISABLED   2
#define OSMGA_BLIT_R_GEOMETRY   3
#define OSMGA_BLIT_R_BUSY       4
#define OSMGA_BLIT_R_PREEXEC    5
#define OSMGA_BLIT_R_POSTEXEC   6
#define OSMGA_BLIT_R_OBSERVED   7   /* reject-only mode: recorded, not run */

/* S2 geometry: source and the offscreen destination, as row offsets below the
 * visible image; the visible destination is the bottom-right corner. */
#define OSMGA_S2_SRC_Y_OFF      256UL
#define OSMGA_S2_DST_Y_OFF      384UL

/* S1 test geometry; see plan section 4. */
#define OSMGA_S1_GUARD_ROWS     256UL
#define OSMGA_S1_X              0UL
#define OSMGA_S1_W              64UL
#define OSMGA_S1_H              64UL
#define OSMGA_S1_SENTINEL       0x5A5A5A5AUL
#define OSMGA_S1_FILL           0xDEADBEEFUL
/* Only VRAM proven real by the working 1600x1200x32 scanout (7.32 MiB). */
#define OSMGA_S1_VRAM_PROVEN    (7UL * 1024UL * 1024UL)
#define OSMGA_S1_SPIN_LIMIT     100000UL

/*
 * Mode table.  Timing -> CRTC/CRTCEXT/Misc is computed from these by
 * osmgaComputeCRTC (X.Org MGAGInit + vgaHWInit formulas), so adding modes is
 * just adding rows.  Starting with a safe 1024x768@60 (65 MHz) to prove the
 * pipeline; the demanding 1600x1200@60 and others follow, plus Configure.app
 * "Display Mode" selection (multi-mode phase).
 */
/* Resolution + timing (independent of pixel depth). */
typedef struct {
    const char *name;
    int width;
    int height;
    int hSyncStart;
    int hSyncEnd;
    int hTotal;
    int vSyncStart;
    int vSyncEnd;
    int vTotal;
    unsigned long clockKHz;
    int hSyncNeg;
    int vSyncNeg;
} OSMGARes;

static const OSMGARes osmgaRes[] = {
    /* name, w, h, hSyncStart, hSyncEnd, hTotal, vSyncStart, vSyncEnd, vTotal,
       clockKHz, hSyncNeg, vSyncNeg  (VESA DMT) */
    { "640x480",   640,  480,  656,  752,  800,  490,  492,  525,  25175UL, 1, 1 },
    { "800x600",   800,  600,  840,  968, 1056,  601,  605,  628,  40000UL, 0, 0 },
    { "1024x768", 1024,  768, 1048, 1184, 1344,  771,  777,  806,  65000UL, 1, 1 },
    { "1280x1024",1280, 1024, 1328, 1440, 1688, 1025, 1028, 1066, 108000UL, 0, 0 },
    { "1600x1200",1600, 1200, 1664, 1856, 2160, 1201, 1204, 1250, 162000UL, 0, 0 }
};
#define OSMGA_RES_COUNT ((int)(sizeof(osmgaRes) / sizeof(osmgaRes[0])))
#define OSMGA_RES_DEFAULT 2   /* 1024x768 */

/*
 * Pixel format (depth / colorspace).  bppShift feeds osmgaComputeCRTC; mulCtl
 * is the RAMDAC MUL_CTL (multiplexer/pixel format); isPseudo marks the 8bpp
 * color mode whose LUT is the window-server colormap (loaded via
 * setTransferTable:count:); grayscale and RGB modes use a fixed linear ramp.
 */
typedef struct {
    const char *cspace;        /* config "ColorSpace" token to match */
    int bytesPerPixel;
    unsigned char mulCtl;
    int bppShift;
    IOBitsPerPixel ioBpp;
    IOColorSpace ioColorSpace;
    const char *pixelEncoding;
    int isPseudo;              /* 8bpp PseudoColor: use window-server palette */
    int grayLevels;            /* >1: quantize the gray ramp to N output levels
                                * (retro N-gray look on an 8bpp scanout; 0/1 =
                                * full 256-level identity ramp) */
} OSMGAFormat;

/*
 * NOTE: the G450 scanout engine only does 8/16/24/32bpp packed pixels (original
 * MGAG200Init indexes MGABppShifts[bpp>>3] and rejects anything else with
 * "unsupported depth"), so a true 2bpp linear framebuffer is impossible on this
 * hardware.  "BW:4" is therefore an 8bpp grayscale scanout whose DAC LUT is
 * quantized to 4 output grays -- it reproduces the retro 4-gray MegaPixel *look*
 * while remaining a hardware-valid 8bpp mode (the window server still renders
 * 256-level gray; only the displayed palette is quantized).
 */
static const OSMGAFormat osmgaFmt[] = {
    { "RGB:888/32", 4, 0x07, 2, IO_24BitsPerPixel, IO_RGBColorSpace,
      "--------RRRRRRRRGGGGGGGGBBBBBBBB", 0, 0 },
    { "RGB:555/16", 2, 0x01, 1, IO_15BitsPerPixel, IO_RGBColorSpace,
      "-RRRRRGGGGGBBBBB", 0, 0 },
    { "RGB:256/8",  1, 0x00, 0, IO_8BitsPerPixel, IO_RGBColorSpace,
      "PPPPPPPP", 1, 0 },
    { "BW:8",       1, 0x00, 0, IO_8BitsPerPixel, IO_OneIsWhiteColorSpace,
      "WWWWWWWW", 0, 0 },
    { "BW:4",       1, 0x00, 0, IO_8BitsPerPixel, IO_OneIsWhiteColorSpace,
      "WWWWWWWW", 0, 4 }
};
#define OSMGA_FMT_COUNT ((int)(sizeof(osmgaFmt) / sizeof(osmgaFmt[0])))
#define OSMGA_FMT_DEFAULT 0   /* RGB:888/32 */

static const unsigned char osmgaSEQ[5]     = { 0x00,0x01,0x0f,0x00,0x0e };
static const unsigned char osmgaGR[9]      = { 0,0,0,0,0,0x40,0x05,0x0f,0xff };
static const unsigned char osmgaAR[21] = {
    0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,
    0x41,0x00,0x0f,0x00,0x00
};
/* initDAC seed (0x00-0x4f), MUL_CTL(0x19) overridden to 0x07 for 32bpp. */
static const unsigned char osmgaInitDAC[0x50] = {
    0,0,0,0,0,0,0,0,  0,0,0,0,0,0,0,0,  0,0,0,0,0,0,0,0,
    0x00,0x07,0xc9,0xff,0xbf,0x20,0x1f,0x20,
    0,0,0,0,0,0,0,0,  0,0,0,0,0,0,0,0x40,
    0x00,0xb0,0x00,0xc2,0x34,0x14,0x02,0x83,
    0x00,0x93,0x00,0x77,0x00,0x00,0x00,0x3a,
    0,0,0,0,0,0,0,0,  0,0,0,0,0,0,0,0
};

/* ---- PCI configuration (mechanism #1) ---- */

static unsigned long
osmgaPciReadConfigLong(int bus, int device, int function, int reg)
{
    unsigned long address;

    address = 0x80000000UL
        | ((unsigned long)(bus & 0xff) << 16)
        | ((unsigned long)(device & 0x1f) << 11)
        | ((unsigned long)(function & 0x07) << 8)
        | ((unsigned long)(reg & 0xfc));

    outl((IOEISAPortAddress)PCI_CFG_ADDR, address);
    return inl((IOEISAPortAddress)PCI_CFG_DATA);
}

static BOOL
osmgaFindMGAFunction(int *outBus, int *outDev, int *outFn,
                     unsigned int *outVendorDevice, unsigned int *outRevision)
{
    int bus;
    int device;
    int function;
    int functions;
    unsigned long vendorDevice;
    unsigned long headerType;
    unsigned long classRevision;
    unsigned int vendor;
    unsigned int product;

    for (bus = 0; bus < PCI_MAX_BUS; bus++) {
        for (device = 0; device < PCI_MAX_DEVICE; device++) {
            vendorDevice = osmgaPciReadConfigLong(bus, device, 0, 0x00);
            if (vendorDevice == 0xffffffffUL || vendorDevice == 0UL)
                continue;

            headerType = osmgaPciReadConfigLong(bus, device, 0, 0x0c);
            functions = (((headerType >> 16) & 0x80UL) != 0) ? 8 : 1;
            for (function = 0; function < functions; function++) {
                vendorDevice =
                    osmgaPciReadConfigLong(bus, device, function, 0x00);
                vendor = (unsigned int)(vendorDevice & 0xffffUL);
                product = (unsigned int)((vendorDevice >> 16) & 0xffffUL);
                if (vendor != MGA_VENDOR_ID || product != MGA_G400_G450_ID)
                    continue;

                classRevision =
                    osmgaPciReadConfigLong(bus, device, function, 0x08);
                if (outBus)
                    *outBus = bus;
                if (outDev)
                    *outDev = device;
                if (outFn)
                    *outFn = function;
                if (outVendorDevice)
                    *outVendorDevice = (unsigned int)vendorDevice;
                if (outRevision)
                    *outRevision = (unsigned int)(classRevision & 0xffUL);
                return YES;
            }
        }
    }
    return NO;
}

/* ---- MMIO register access ---- */

static unsigned char
osmgaR8(vm_address_t base, unsigned int off)
{
    return *(volatile unsigned char *)(base + off);
}

static void
osmgaW8(vm_address_t base, unsigned int off, unsigned char v)
{
    *(volatile unsigned char *)(base + off) = v;
}

static unsigned long
osmgaR32(vm_address_t base, unsigned int off)
{
    return *(volatile unsigned long *)(base + off);
}

static void
osmgaW32(vm_address_t base, unsigned int off, unsigned long v)
{
    *(volatile unsigned long *)(base + off) = v;
}

static void
osmgaOutDac(vm_address_t base, unsigned char idx, unsigned char v)
{
    osmgaW8(base, MGA_DAC_INDEX, idx);
    osmgaW8(base, MGA_DAC_DATA, v);
}

static void
osmgaWriteCrtc(vm_address_t base, unsigned char idx, unsigned char v)
{
    osmgaW8(base, MGA_CRTC_INDEX, idx);
    osmgaW8(base, MGA_CRTC_DATA, v);
}

static unsigned char
osmgaReadCrtc(vm_address_t base, unsigned char idx)
{
    osmgaW8(base, MGA_CRTC_INDEX, idx);
    return osmgaR8(base, MGA_CRTC_DATA);
}

static void
osmgaWriteCrtcExt(vm_address_t base, unsigned char idx, unsigned char v)
{
    osmgaW8(base, MGA_CRTCEXT_INDEX, idx);
    osmgaW8(base, MGA_CRTCEXT_DATA, v);
}

static void
osmgaWriteAttr(vm_address_t base, unsigned char idx, unsigned char v)
{
    (void)osmgaR8(base, MGA_INSTS1);       /* reset attr flip-flop to index */
    osmgaW8(base, MGA_ATTR_INDEX, idx);
    osmgaW8(base, MGA_ATTR_INDEX, v);
}

/* ---- Storm 2D engine: bounded waits (never spin forever) ---- */

/* Wait until the drawing engine reports idle.  Returns 1 on idle, 0 on
 * timeout.  A timeout must abort the caller: never issue an EXEC, and never
 * try to "clean up" a possibly wedged FIFO. */
static int
osmgaStormWaitIdle(vm_address_t base)
{
    unsigned long spins;

    for (spins = 0UL; spins < OSMGA_S1_SPIN_LIMIT; spins++) {
        if ((osmgaR8(base, MGA_ENGSTATUS + 2) & MGA_ENGBUSY_BIT) == 0)
            return 1;
    }
    return 0;
}

/* Wait until at least `slots' FIFO entries are free.  Returns 1, or 0 on
 * timeout.  Engine-idle alone does NOT guarantee FIFO admission, so every
 * write batch is gated by this. */
static int
osmgaStormWaitFifo(vm_address_t base, unsigned int slots)
{
    unsigned long spins;

    for (spins = 0UL; spins < OSMGA_S1_SPIN_LIMIT; spins++) {
        if ((unsigned int)osmgaR8(base, MGA_FIFOSTATUS) >= slots)
            return 1;
    }
    return 0;
}

/*
 * Deterministic engine state shared by every primitive (12 writes; the caller
 * adds DWGCTL for its own opcode, so gate 13 FIFO slots).  Nothing is inherited
 * from a previous user.
 *
 * The clip is DESTINATION-ONLY: source reads go through AR3/AR0/AR5 and are not
 * clipped, so the window is narrowed to just the destination rectangle -- that
 * is the strongest containment available and widening it to cover the source
 * would only weaken it (docs/S2_STORM_BITBLT_PLAN.md 8-2).
 */
static void
osmgaStormInitState(vm_address_t base, unsigned long stridePixels,
                    unsigned long clipX0, unsigned long clipX1,
                    unsigned long clipTopPixel, unsigned long clipBotPixel)
{
    unsigned long opmode = osmgaR32(base, MGA_OPMODE);

    osmgaW32(base, MGA_PITCH,   stridePixels);
    osmgaW32(base, MGA_YDSTORG, 0UL);
    osmgaW32(base, MGA_MACCESS, MGA_MACCESS_PW32);
    osmgaW32(base, MGA_PLNWT,   0xffffffffUL);
    osmgaW32(base, MGA_FCOL,    0UL);
    osmgaW32(base, MGA_BCOL,    0UL);
    osmgaW32(base, MGA_OPMODE,
             MGA_OPMODE_DMA_BLIT | (opmode & ~(unsigned long)MGA_OPMODE_BYTESWAP));
    osmgaW32(base, MGA_CXBNDRY, (clipX1 << 16) | clipX0);  /* right inclusive */
    osmgaW32(base, MGA_YTOP,    clipTopPixel);             /* row start ptrs  */
    osmgaW32(base, MGA_YBOT,    clipBotPixel);
    osmgaW32(base, MGA_SRCORG,  0UL);
    osmgaW32(base, MGA_DSTORG,  0UL);
}

/*
 * Map [byteStart, byteEnd) of the framebuffer as an uncached alias, because the
 * framebuffer mapping's read cache attribute is unproven and `volatile` does
 * not make cached reads coherent with engine writes.  IOMapPhysicalIntoIOTask
 * is known-uncached from H1 S2 (VCOUNT read through it increments).
 */
static IOReturn
osmgaMapUncachedBlock(unsigned long fbPhysical, unsigned long byteStart,
                      unsigned long byteEnd, vm_address_t *outAlias,
                      unsigned long *outMapLen,
                      volatile unsigned long **outPtr)
{
    unsigned long mapStart = byteStart & ~0x0FFFUL;
    unsigned long mapLen   = ((byteEnd - mapStart) + 0x0FFFUL) & ~0x0FFFUL;
    vm_address_t alias = 0;
    IOReturn r;

    *outAlias = 0;
    *outMapLen = 0;
    *outPtr = 0;
    r = IOMapPhysicalIntoIOTask((unsigned)(fbPhysical + mapStart),
                                (unsigned)mapLen, &alias);
    if (r != IO_R_SUCCESS || alias == 0)
        return (r == IO_R_SUCCESS) ? IO_R_NO_MEMORY : r;
    *outAlias = alias;
    *outMapLen = mapLen;
    *outPtr = (volatile unsigned long *)(alias + (byteStart - mapStart));
    return IO_R_SUCCESS;
}

/*
 * General screen-to-screen copy with overlap handling.
 *
 * Direction follows the memmove rule and is applied unconditionally (it is
 * also correct for non-overlapping rectangles).  Verified three ways: X.Org
 * mga_storm.c, our own host simulation of all six overlap cases against
 * memmove semantics, and openstep-sdl12/.../SDL_fbmatrox.c, which matches it
 * verbatim.  See docs/S3_IODISPLAY_DO_BLIT_PLAN.md.
 *
 * Returns  1  copy completed and the engine went idle
 *          0  failed BEFORE the execute write -- nothing was issued, so the
 *             caller may safely fall back to a software copy
 *         -1  timed out AFTER the execute write -- the engine may still be
 *             writing, so a software fallback would be overwritten later.
 *             The caller must permanently disable acceleration.
 */
static int
osmgaStormBlit(vm_address_t base, unsigned long stride,
               unsigned long srcX, unsigned long srcY,
               unsigned long w, unsigned long h,
               unsigned long dstX, unsigned long dstY)
{
    int up   = (srcY < dstY);      /* copy bottom-to-top  */
    int left = (srcX < dstX);      /* copy right-to-left  */
    unsigned long sgn = (up ? MGA_SGN_BLIT_UP : 0UL) |
                        (left ? MGA_SGN_BLIT_LEFT : 0UL);
    unsigned long ar5 = up ? (unsigned long)(-(long)stride) : stride;
    unsigned long w1 = w - 1UL;
    unsigned long sy = srcY;
    unsigned long dy = dstY;
    unsigned long start, end;

    if (up) {                      /* start from the last row */
        sy += h - 1UL;
        dy += h - 1UL;
    }
    start = end = sy * stride + srcX;
    if (left)
        start += w1;               /* start from the rightmost pixel */
    else
        end += w1;

    /* 12 state writes + DWGCTL.  The clip is the GEOMETRIC destination
     * rectangle -- it is not reversed when copying up/left. */
    if (!osmgaStormWaitFifo(base, 13U))
        return 0;
    osmgaStormInitState(base, stride, dstX, dstX + w1,
                        dstY * stride, (dstY + h - 1UL) * stride);
    osmgaW32(base, MGA_DWGCTL, MGA_DWGCTL_BITBLT);

    if (!osmgaStormWaitFifo(base, 3U))
        return 0;
    osmgaW32(base, MGA_SGN, sgn);
    osmgaW32(base, MGA_AR5, ar5);

    if (!osmgaStormWaitFifo(base, 4U))
        return 0;
    osmgaW32(base, MGA_AR0, end);
    osmgaW32(base, MGA_AR3, start);
    /* BITBLT takes an inclusive right edge; dstX is never direction-adjusted. */
    osmgaW32(base, MGA_FXBNDRY, ((dstX + w1) << 16) | (dstX & 0xffffUL));
    /* dy IS direction-adjusted: going up, the operation starts at the last row. */
    osmgaW32(base, MGA_YDSTLEN + MGA_EXEC, (dy << 16) | h);

    if (!osmgaStormWaitIdle(base))
        return -1;                 /* post-execute: not safely recoverable */
    return 1;
}

/* Position-encoding test pattern: a shifted or misaligned copy is detected,
 * not merely "something got written". */
static unsigned long
osmgaS2Pattern(unsigned long row, unsigned long col)
{
    return 0xFF000000UL | (row << 8) | col;
}

/* ---- G450 pixel PLL (jitter + statistical lock search) ---- */

static unsigned long
osmgaG450CalcVCO(unsigned char M, unsigned char N)
{
    return (27000UL * (2UL * ((unsigned long)N + 2UL)) +
            (((unsigned long)M + 1UL) >> 1)) / ((unsigned long)M + 1UL);
}

/*
 * The following is a faithful port of the original MatroxMGA G450SetPLLFreq
 * search (verified against the binary via IDA + codex cross-review): build a
 * frequency-sorted candidate list, then pick the first candidate that locks
 * stably across a jitter band (N +/- 1/2/3), else the first center-locking
 * candidate, else the best-frequency candidate.  Our earlier single-candidate
 * "first center-lock wins" search picked a frequency-perfect but jitter-unstable
 * MNP (e.g. 0x000400 for 162 MHz: M=0, VCO=324 MHz) that lost monitor sync; the
 * original tries high-VCO candidates first (0x001622: VCO=1296 MHz) and only
 * accepts one stable across the band.
 */

#define OSMGA_PLL_FREF     27000UL
#define OSMGA_PLL_VCO_MIN  256000UL    /* 0x3E800 */
#define OSMGA_PLL_VCO_MAX  1300000UL   /* 0x13D620 */
#define OSMGA_PLL_MAX_CAND 64

/* Fout(mnp) = VCO / postdiv (= original sub_449C + sub_4438). */
static unsigned long
osmgaG450Fout(unsigned long mnp)
{
    unsigned char M = (unsigned char)((mnp >> 16) & 0xffUL);
    unsigned char N = (unsigned char)((mnp >> 8) & 0xffUL);
    unsigned char P = (unsigned char)(mnp & 0xffUL);
    unsigned long vco = osmgaG450CalcVCO(M, N);
    if (P & 0x40)
        return vco;
    return vco / (2UL << (P & 0x03));
}

/* relative error in permille (= original sub_44F8). */
static unsigned long
osmgaG450Err(unsigned long target, unsigned long mnp)
{
    unsigned long got = osmgaG450Fout(mnp);
    unsigned long d = (got > target) ? (got - target) : (target - got);
    return 1000UL * d / target;
}

/* freq -> VCO target: freq * postdiv (= original sub_4474). */
static unsigned long
osmgaG450ScaleToVco(unsigned char sel, unsigned long freq)
{
    if (sel & 0x40)
        return freq;
    return freq * (2UL << (sel & 0x03));
}

/* band bits [5:3] from a target VCO (= original sub_4534 v4 ladder). */
static unsigned int
osmgaG450Band(unsigned long vco)
{
    if (vco <= 550000UL)  return 0;
    if (vco <= 700000UL)  return 1;
    if (vco <= 900000UL)  return 2;
    if (vco <= 1100000UL) return 3;
    if (vco <= 1300000UL) return 4;
    return 5;
}

/* Does candidate `nw` sort strictly before `ex`? (= G450CompareMNP res<0,
 * A=new): smaller error first; on a <=5-permille tie, smaller M first. */
static int
osmgaG450Better(unsigned long freq, unsigned long nw, unsigned long ex)
{
    unsigned long en = osmgaG450Err(freq, nw);
    unsigned long ee = osmgaG450Err(freq, ex);

    if (en < ee) return 1;
    if (en > ee) return 0;
    if (en <= 5UL && ee <= 5UL) {
        unsigned int Mn = (unsigned int)((nw >> 16) & 0xffUL);
        unsigned int Me = (unsigned int)((ex >> 16) & 0xffUL);
        if (Mn < Me) return 1;
    }
    return 0;
}

/* Build the best-first candidate list; returns the count (= original
 * sub_4628 init + sub_4534 generator + G450CompareMNP insertion sort). */
static int
osmgaG450BuildCandidates(unsigned long freq, unsigned long *cand)
{
    unsigned char sel;
    int count = 0;
    int M;

    if (freq > 650000UL) {
        sel = 0x40;
        if (osmgaG450ScaleToVco(sel, freq) > OSMGA_PLL_VCO_MAX)
            return 0;
    } else {
        sel = 3;
        while (sel > 0 && osmgaG450ScaleToVco(sel, freq) > OSMGA_PLL_VCO_MAX)
            sel--;
        if (osmgaG450ScaleToVco(sel, freq) > OSMGA_PLL_VCO_MAX)
            return 0;
    }

    M = 0;
    for (;;) {
        unsigned long vcoTarget = osmgaG450ScaleToVco(sel, freq);
        if (vcoTarget < OSMGA_PLL_VCO_MIN)
            break;
        {
            long N = (long)((vcoTarget * (unsigned long)(M + 1) + OSMGA_PLL_FREF)
                            / 54000UL) - 2L;
            if (N >= 0L && N <= 255L && count < OSMGA_PLL_MAX_CAND) {
                unsigned int band = osmgaG450Band(vcoTarget);
                unsigned long mnp =
                    ((unsigned long)M << 16) |
                    ((unsigned long)N << 8) |
                    (unsigned long)(((band << 3) | (sel & 0x43)) & 0xff);
                int i = count;
                while (i > 0 && osmgaG450Better(freq, mnp, cand[i - 1])) {
                    cand[i] = cand[i - 1];
                    i--;
                }
                cand[i] = mnp;
                count++;
            }
        }
        if (M == 9) {
            if (sel & 0x40)
                break;
            sel = (sel & 0x43) ? (unsigned char)((sel & 0x43) - 1)
                               : (unsigned char)0x40;
            M = 0;
        } else {
            M++;
        }
    }
    return count;
}

static void
osmgaG450WriteMNP(vm_address_t base, unsigned long mnp)
{
    osmgaOutDac(base, MGA_DAC_PIX_M, (unsigned char)((mnp >> 16) & 0xffUL));
    osmgaOutDac(base, MGA_DAC_PIX_N, (unsigned char)((mnp >> 8) & 0xffUL));
    osmgaOutDac(base, MGA_DAC_PIX_P, (unsigned char)(mnp & 0xffUL));
}

static int
osmgaG450IsLocked(vm_address_t base)
{
    unsigned long spins;
    unsigned long i;
    unsigned long lockCount;
    unsigned char st;

    osmgaW8(base, MGA_DAC_INDEX, MGA_DAC_PIX_STAT);
    spins = 0UL;
    do {
        st = osmgaR8(base, MGA_DAC_DATA);
        spins++;
    } while ((st & MGA_PLLLOCK) == 0 && spins < 1000UL);
    if (spins >= 1000UL)
        return 0;
    lockCount = 0UL;
    for (i = 0UL; i < 100UL; i++) {
        st = osmgaR8(base, MGA_DAC_DATA);
        if (st & MGA_PLLLOCK)
            lockCount++;
    }
    return lockCount >= 90UL;
}

/* Program `mnp` and report whether the PLL locks on it. */
static int
osmgaG450TryLock(vm_address_t base, unsigned long mnp)
{
    osmgaG450WriteMNP(base, mnp);
    return osmgaG450IsLocked(base);
}

/*
 * Faithful port of the original G450SetPLLFreq selection loop.  Walks the
 * frequency-sorted candidates and accepts the first one that locks across the
 * whole jitter band (N-3..N+3 probed as -0x300,+0x300,-0x200,+0x200,-0x100,
 * +0x100, then the centre).  A candidate whose N field is outside 3..122, or
 * that fails any band probe, is not accepted; the FIRST such candidate that at
 * least locks at its centre is remembered as the fallback (later ones are not
 * probed).  If no candidate is stable, the fallback -- else the best-frequency
 * candidate -- is programmed.  Returns 1 if the finally programmed value locked.
 *
 * Note the control flow detail (cross-reviewed): a stable candidate leaves its
 * own centre write in place and is NOT recorded as the fallback, so the final
 * fallback write only happens when the whole list was exhausted.
 */
static int
osmgaG450SetPLL(vm_address_t base, unsigned long fOut, unsigned long *outMNP)
{
    unsigned long cand[OSMGA_PLL_MAX_CAND];
    unsigned long fallback = 0UL;
    int haveFallback = 0;
    unsigned char misc;
    int count;
    int s;

    misc = osmgaR8(base, MGA_MISC_READ);
    osmgaW8(base, MGA_MISC_WRITE, (unsigned char)(misc | MGA_CLKSEL_MGA));

    count = osmgaG450BuildCandidates(fOut, cand);
    if (count <= 0) {
        if (outMNP)
            *outMNP = 0UL;
        return 0;
    }

    for (s = 0; s < count; s++) {
        unsigned long v4 = cand[s];
        int stable = 0;

        /* N-field window: unsigned ((N<<8) - 0x300) <= 0x7700, i.e. 3..122 */
        if (((v4 & 0xFF00UL) - 0x300UL) <= 0x7700UL) {
            if (osmgaG450TryLock(base, v4 - 0x300UL) &&
                osmgaG450TryLock(base, v4 + 0x300UL) &&
                osmgaG450TryLock(base, v4 - 0x200UL) &&
                osmgaG450TryLock(base, v4 + 0x200UL) &&
                osmgaG450TryLock(base, v4 - 0x100UL) &&
                osmgaG450TryLock(base, v4 + 0x100UL))
                stable = osmgaG450TryLock(base, v4);
        }
        if (stable) {
            /* v4 is programmed and locked: done, no fallback write */
            IOLog("OpenStepMGAReplacementDisplay: PLL stable cand %d/%d\n",
                  s + 1, count);
            if (outMNP)
                *outMNP = v4;
            return 1;
        }
        if (!haveFallback) {
            if (osmgaG450TryLock(base, v4)) {
                fallback = v4;
                haveFallback = 1;
            }
        }
    }

    if (haveFallback) {
        IOLog("OpenStepMGAReplacementDisplay: PLL no stable cand of %d; "
              "using centre-lock fallback\n", count);
        osmgaG450WriteMNP(base, fallback);
        if (outMNP)
            *outMNP = fallback;
        return osmgaG450IsLocked(base);
    }
    IOLog("OpenStepMGAReplacementDisplay: PLL nothing locked (%d cands)\n",
          count);
    osmgaG450WriteMNP(base, cand[0]);
    if (outMNP)
        *outMNP = cand[0];
    return 0;
}

static int
osmgaDacSkip(unsigned int i)
{
    if (i <= 0x03U) return 1;
    if (i == 0x07U || i == 0x0bU || i == 0x0fU) return 1;
    if (i >= 0x13U && i <= 0x17U) return 1;
    if (i == 0x1bU || i == 0x1cU) return 1;
    if (i >= 0x1fU && i <= 0x29U) return 1;
    if (i >= 0x30U && i <= 0x37U) return 1;
    if (i == 0x2cU || i == 0x2dU || i == 0x2eU) return 1; /* Gx50 SYS_PLL */
    if (i == 0x4cU || i == 0x4dU || i == 0x4eU) return 1; /* Gx50 PIX_PLL */
    return 0;
}

/*
 * Compute CRTC[25], CRTCEXT[6] and MiscOutput for a mode (32bpp), following
 * X.Org MGAGInit (mga_dacG.c) + vgaHWInit.  Verified to reproduce the known
 * 1600x1200x32 image exactly.
 */
static void
osmgaComputeCRTC(const OSMGARes *m, int bppShift, unsigned char crtc[25],
                 unsigned char ext[6], unsigned char *miscOut)
{
    long hd, hs, he, ht, vd, vs, ve, vt, wd;
    unsigned char misc;
    int i;

    hd = ((long)m->width >> 3) - 1;
    hs = ((long)m->hSyncStart >> 3) - 1;
    he = ((long)m->hSyncEnd >> 3) - 1;
    ht = ((long)m->hTotal >> 3) - 1;
    vd = (long)m->height - 1;
    vs = (long)m->vSyncStart - 1;
    ve = (long)m->vSyncEnd - 1;
    vt = (long)m->vTotal - 2;
    if ((ht & 0x07) == 0x06 || (ht & 0x07) == 0x04)
        ht++;
    wd = (long)m->width >> (4 - bppShift);

    for (i = 0; i < 25; i++)
        crtc[i] = 0;
    crtc[0]  = (unsigned char)(ht - 4);
    crtc[1]  = (unsigned char)hd;
    crtc[2]  = (unsigned char)hd;
    crtc[3]  = (unsigned char)((ht & 0x1F) | 0x80);
    crtc[4]  = (unsigned char)hs;
    crtc[5]  = (unsigned char)(((ht & 0x20) << 2) | (he & 0x1F));
    crtc[6]  = (unsigned char)(vt & 0xFF);
    crtc[7]  = (unsigned char)(((vt & 0x100) >> 8) | ((vd & 0x100) >> 7) |
               ((vs & 0x100) >> 6) | ((vd & 0x100) >> 5) | ((vd & 0x100) >> 4) |
               ((vt & 0x200) >> 4) | ((vd & 0x200) >> 3) | ((vs & 0x200) >> 2));
    crtc[9]  = (unsigned char)(((vd & 0x200) >> 4) | ((vd & 0x200) >> 3));
    crtc[16] = (unsigned char)(vs & 0xFF);
    crtc[17] = (unsigned char)((ve & 0x0F) | 0x20);
    crtc[18] = (unsigned char)(vd & 0xFF);
    crtc[19] = (unsigned char)(wd & 0xFF);
    crtc[20] = 0x00;
    crtc[21] = (unsigned char)(vd & 0xFF);
    crtc[22] = (unsigned char)((vt + 1) & 0xFF);
    crtc[23] = 0xC3;               /* vgaHWInit depth>=8: reset-release */
    crtc[24] = (unsigned char)(vd & 0xFF);

    ext[0] = (unsigned char)((wd & 0x300) >> 4);
    ext[1] = (unsigned char)((((ht - 4) & 0x100) >> 8) | ((hd & 0x100) >> 7) |
             ((hs & 0x100) >> 6) | (ht & 0x40));
    ext[2] = (unsigned char)(((vt & 0xc00) >> 10) | ((vd & 0x400) >> 8) |
             ((vd & 0xc00) >> 7) | ((vs & 0xc00) >> 5) | ((vd & 0x400) >> 3));
    ext[3] = (unsigned char)(((1 << bppShift) - 1) | 0x80);
    ext[4] = 0x00;
    ext[5] = 0x00;

    misc = 0x23;
    if (m->hSyncNeg)
        misc |= 0x40;
    if (m->vSyncNeg)
        misc |= 0x80;
    misc |= 0x0C;                  /* external clock select */
    misc = (unsigned char)(misc & ~0x02);  /* disable VGA memory aperture */
    *miscOut = misc;
}

/* Exact string equality (no libc in the kernel loadable).  Parameter-name
 * dispatch must be exact: a substring match could claim a longer name. */
static int
osmgaTextEquals(const char *a, const char *b)
{
    if (a == 0 || b == 0)
        return 0;
    while (*a != '\0' && *a == *b) {
        a++;
        b++;
    }
    return (*a == '\0' && *b == '\0');
}

/* Substring search (no libc in the kernel loadable). */
static int
osmgaTextContains(const char *hay, const char *needle)
{
    const char *h;
    const char *n;
    const char *hp;

    if (hay == 0 || needle == 0)
        return 0;
    for (h = hay; *h != '\0'; h++) {
        hp = h;
        n = needle;
        while (*n != '\0' && *hp == *n) {
            hp++;
            n++;
        }
        if (*n == '\0')
            return 1;
    }
    return 0;
}

static unsigned char
osmgaG450PanCtl(unsigned long fkhz)
{
    if (fkhz < 45000UL)  return 0x00;
    if (fkhz < 65000UL)  return 0x08;
    if (fkhz < 85000UL)  return 0x10;
    if (fkhz < 105000UL) return 0x18;
    if (fkhz < 135000UL) return 0x20;
    if (fkhz < 160000UL) return 0x28;
    if (fkhz < 175000UL) return 0x30;
    return 0x38;
}

/* Geometry fields are overwritten per selected mode in init; this carries the
 * 32bpp format (bitsPerPixel/colorSpace/pixelEncoding). */
static IODisplayInfo osmgaModeTemplate = {
    1024, 768, 1024, 4096, 60, 0,
    IO_24BitsPerPixel, IO_RGBColorSpace, "--------RRRRRRRRGGGGGGGGBBBBBBBB",
    0, 0
};

@implementation OpenStepMGAReplacementDisplay

+ (BOOL)probe:deviceDescription
{
    unsigned int vendorDevice = 0;
    unsigned int revision = 0;

    if (!osmgaFindMGAFunction(0, 0, 0, &vendorDevice, &revision)) {
        IOLog("OpenStepMGAReplacementDisplay: no MGA G400/G450 present, probe NO\n");
        return NO;
    }
    /* Only bind G450-class revisions; the G400 mode path is not implemented. */
    if (revision < MGA_G450_MIN_REVISION) {
        IOLog("OpenStepMGAReplacementDisplay: MGA rev %02x is pre-G450; G400 path "
              "unimplemented, probe NO\n", revision);
        return NO;
    }
    IOLog("OpenStepMGAReplacementDisplay: MGA %04x:%04x rev %02x (G450), probe YES\n",
          (unsigned int)(vendorDevice & 0xffff),
          (unsigned int)((vendorDevice >> 16) & 0xffff), revision);
    return [super probe:deviceDescription];
}

- (void)teardownMappings
{
    /*
     * Only the MMIO aperture is unmapped here (we mapped it with
     * IOMapPhysicalIntoIOTask).  The framebuffer was mapped with
     * mapFrameBufferAtPhysicalAddress:length:; like MatroxMGA, its unmapping is
     * left to the superclass on free -- we must not IOUnmapPhysicalFromIOTask a
     * mapping we did not make that way.
     */
    if (mmioMapped) {
        IOUnmapPhysicalFromIOTask(mmioBase, MGA_MMIO_LENGTH);
        mmioMapped = NO;
        mmioBase = 0;
    }
    frameBufferMapped = NO;
}

- initFromDeviceDescription:deviceDescription
{
    IODisplayInfo *displayInfo;
    IOReturn result;
    int bus = 0;
    int dev = 0;
    int fn = 0;
    unsigned int vendorDevice = 0;
    unsigned int revision = 0;
    unsigned long bar0;
    unsigned long bar1;
    vm_address_t fbVirt = 0;
    IORange ranges[3];

    if ([super initFromDeviceDescription:deviceDescription] == nil)
        return [super free];

    chipVendorDevice = 0;
    chipRevision = 0;
    chipIsG450 = NO;
    frameBufferMapped = NO;
    mmioMapped = NO;
    mmioBase = 0;
    mmioPhysical = 0;
    frameBufferPhysical = 0;
    linearModeActive = NO;
    selectedResIndex = OSMGA_RES_DEFAULT;
    selectedFormatIndex = OSMGA_FMT_DEFAULT;
    paletteValid = NO;
    stormTestEnabled = NO;
    stormBlitReady = NO;
    stormBlitFailed = NO;
    stormBusy = NO;
    simple_lock_init(&stormLock);
    statBlitRequests = 0; statBlitOk = 0; statBlitNoop = 0;
    statRefusedDisabled = 0; statRefusedGeometry = 0; statRefusedBusy = 0;
    statRefusedPreExec = 0; statPostExecTimeout = 0;
    statCursorShow = 0; statCursorMove = 0; statCursorHide = 0;
    statCursorWhileBusy = 0; statThin1px = 0;
    statEnterLinear = 0; statRevertVGA = 0;
    stormObserveOnly = NO;
    statObserved = 0; statObsOverlap = 0; statObsDirUp = 0; statObsDirLeft = 0;
    statObsCursorNear = 0; statObsMaxW = 0; statObsMaxH = 0; statObsMinDim = 0;
    obsLastCursorTotal = 0;
    manualVideoMemoryConfigured = NO;
    configuredVideoMemoryBytes = 0;

    if (!osmgaFindMGAFunction(&bus, &dev, &fn, &vendorDevice, &revision)) {
        IOLog("OpenStepMGAReplacementDisplay: MGA absent after probe, abort\n");
        return [super free];
    }
    chipVendorDevice = vendorDevice;
    chipRevision = revision;
    chipIsG450 = (revision >= MGA_G450_MIN_REVISION) ? YES : NO;
    /* Read BAR0 (framebuffer) and BAR1 (MMIO) from PCI config, like the
     * production MatroxMGA (empty Memory Maps / I/O Ports -> no resource
     * reservation that could conflict with the boot console). */
    bar0 = osmgaPciReadConfigLong(bus, dev, fn, 0x10);
    bar1 = osmgaPciReadConfigLong(bus, dev, fn, 0x14);
    frameBufferPhysical = bar0 & 0xFFFFFFF0UL;
    mmioPhysical = bar1 & 0xFFFFFFF0UL;
    IOLog("OpenStepMGAReplacementDisplay: chip %04x:%04x rev %02x %s fb=%08x mmio=%08x\n",
          (unsigned int)(vendorDevice & 0xffff),
          (unsigned int)((vendorDevice >> 16) & 0xffff), revision,
          chipIsG450 ? "G450" : "pre-G450",
          (unsigned int)frameBufferPhysical, (unsigned int)mmioPhysical);

    if (![self readManualMemoryConfiguration:[deviceDescription configTable]])
        IOLog("OpenStepMGAReplacementDisplay: manual MGA Memory Size unavailable\n");
    [self selectModeFromConfig:[deviceDescription configTable]];

    /* Opt-in S1 engine liveness test; absent/anything-but-Yes means off. */
    {
        IOConfigTable *ct = [deviceDescription configTable];
        const char *flag = (ct == nil) ? 0
                         : (const char *)[ct valueForStringKey:"Storm 2D Test"];
        stormTestEnabled = (flag != 0 && osmgaTextContains(flag, "Yes"))
                           ? YES : NO;
        if (stormTestEnabled)
            IOLog("OpenStepMGAReplacementDisplay: S1 Storm 2D test ENABLED "
                  "by configuration\n");

        /* Reject-only observation mode: advertise the capability but never
         * run the engine for a standard request (plan section 10). */
        flag = (ct == nil) ? 0
             : (const char *)[ct valueForStringKey:"Storm Blit Observe"];
        stormObserveOnly = (flag != 0 && osmgaTextContains(flag, "Yes"))
                           ? YES : NO;
        if (stormObserveOnly)
            IOLog("OpenStepMGAReplacementDisplay: blit OBSERVE-ONLY mode "
                  "(CAN_BLIT advertised, every request recorded and refused, "
                  "engine untouched)\n");
    }

    if (frameBufferPhysical == 0 || mmioPhysical == 0) {
        IOLog("OpenStepMGAReplacementDisplay: bad fb/mmio phys fb=%08x mmio=%08x, abort\n",
              (unsigned int)frameBufferPhysical, (unsigned int)mmioPhysical);
        return [super free];
    }

    /* publish selected mode geometry + pixel format */
    {
        const OSMGARes *r = &osmgaRes[selectedResIndex];
        const OSMGAFormat *f = &osmgaFmt[selectedFormatIndex];
        const char *pe = f->pixelEncoding;
        int k;
        displayInfo = [self displayInfo];
        *displayInfo = osmgaModeTemplate;
        displayInfo->width = r->width;
        displayInfo->height = r->height;
        displayInfo->totalWidth = r->width;
        displayInfo->rowBytes = r->width * f->bytesPerPixel;
        displayInfo->refreshRate = 60;
        displayInfo->bitsPerPixel = f->ioBpp;
        displayInfo->colorSpace = f->ioColorSpace;
        for (k = 0; k + 1 < IO_MAX_PIXEL_BITS && pe[k] != '\0'; k++)
            displayInfo->pixelEncoding[k] = pe[k];
        displayInfo->pixelEncoding[k] = '\0';
        displayInfo->memorySize = r->width * f->bytesPerPixel * r->height;
        displayInfo->scanRate = 60;
        displayInfo->dotClockRate = (int)(r->clockKHz * 1000UL);
        displayInfo->screenWidth = r->width;
        displayInfo->screenHeight = r->height;
        /*
         * The capability flag must be set where IODisplayInfo is published --
         * once the window server has read flags, setting it later is too late.
         * For now it is advertised ONLY in reject-only observation mode, where
         * every standard request is recorded and refused without touching the
         * engine (docs/S3B_PREP_INSTRUMENTATION_PLAN.md 10).
         */
        if (stormObserveOnly)
            displayInfo->flags |= IO_DISPLAY_CAN_BLIT;
    }

    /* map MMIO control aperture (BAR1) cache-inhibited (as MatroxMGA/MGAProbe) */
    result = IOMapPhysicalIntoIOTask((unsigned)mmioPhysical, MGA_MMIO_LENGTH,
                                     &mmioBase);
    if (result != IO_R_SUCCESS || mmioBase == 0) {
        IOLog("OpenStepMGAReplacementDisplay: MMIO map failed r=%d, abort\n",
              (int)result);
        return [super free];
    }
    mmioMapped = YES;

    /*
     * Declare the memory ranges (framebuffer + legacy VGA/BIOS) on the device
     * description, then map the framebuffer via the IOFrameBufferDisplay method
     * mapFrameBufferAtPhysicalAddress:length: -- exactly as the production
     * MatroxMGA does.  This registers the framebuffer with the display
     * subsystem the way the window server expects; a raw
     * IOMapPhysicalIntoIOTask mapping made the boot window server hang.
     */
    ranges[0].start = frameBufferPhysical;
    ranges[0].size = MGA_VRAM_16MB;
    ranges[1].start = 0xa0000;
    ranges[1].size = 0x20000;
    ranges[2].start = 0xc0000;
    ranges[2].size = 0x10000;
    [deviceDescription setMemoryRangeList:ranges num:3];

    fbVirt = [self mapFrameBufferAtPhysicalAddress:(unsigned int)frameBufferPhysical
                                            length:(int)MGA_VRAM_16MB];
    if (fbVirt == 0) {
        IOLog("OpenStepMGAReplacementDisplay: mapFrameBuffer failed, abort\n");
        [self teardownMappings];
        return [super free];
    }
    displayInfo->frameBuffer = (void *)fbVirt;
    frameBufferMapped = YES;

    IOLog("OpenStepMGAReplacementDisplay: init ok fb=%08x mmio=%08x %s %s\n",
          (unsigned int)displayInfo->frameBuffer, (unsigned int)mmioBase,
          osmgaRes[selectedResIndex].name, osmgaFmt[selectedFormatIndex].cspace);
    return self;
}

- (BOOL)readManualMemoryConfiguration:configTable
{
    OSMGAManualMemoryStatus status;

    manualVideoMemoryConfigured = NO;
    configuredVideoMemoryBytes = 0;
    if (configTable == nil) {
        IOLog("OpenStepMGAReplacementDisplay: no configuration table\n");
        return NO;
    }
    if (!OSMGAParseManualMemoryMB([configTable valueForStringKey:"MGA Memory Size"],
                                  &configuredVideoMemoryBytes, &status)) {
        IOLog("OpenStepMGAReplacementDisplay: MGA Memory Size %s\n",
              OSMGAManualMemoryStatusString(status));
        configuredVideoMemoryBytes = 0;
        return NO;
    }
    /* Fixed 16 MiB driver: only 16 is consistent with the mapped aperture. */
    if (configuredVideoMemoryBytes != MGA_VRAM_16MB) {
        IOLog("OpenStepMGAReplacementDisplay: MGA Memory Size != 16 MiB; clamping to 16\n");
        configuredVideoMemoryBytes = (unsigned int)MGA_VRAM_16MB;
    }
    manualVideoMemoryConfigured = YES;
    return YES;
}

/*
 * Choose the active mode from the config-table "Display Mode" string (set by
 * Configure.app from the .modes list), matching width/height against the
 * driver mode table; falls back to OSMGA_MODE_DEFAULT when absent/unmatched.
 */
- (void)selectModeFromConfig:configTable
{
    OSMGAMode mode;
    const char *text;
    int i;

    selectedResIndex = OSMGA_RES_DEFAULT;
    selectedFormatIndex = OSMGA_FMT_DEFAULT;
    if (configTable == nil)
        return;
    text = (const char *)[configTable valueForStringKey:"Display Mode"];
    if (text == 0)
        return;
    if (OSMGAParseManualDisplayMode(text, &mode)) {
        for (i = 0; i < OSMGA_RES_COUNT; i++)
            if ((unsigned int)osmgaRes[i].width == (unsigned int)mode.width &&
                (unsigned int)osmgaRes[i].height == (unsigned int)mode.height) {
                selectedResIndex = i;
                break;
            }
    }
    for (i = 0; i < OSMGA_FMT_COUNT; i++)
        if (osmgaTextContains(text, osmgaFmt[i].cspace)) {
            selectedFormatIndex = i;
            break;
        }
    IOLog("OpenStepMGAReplacementDisplay: config selected %s %s\n",
          osmgaRes[selectedResIndex].name, osmgaFmt[selectedFormatIndex].cspace);
}

/*
 * Program the selected mode directly, following the proven production MatroxMGA
 * G450 sequence (verified by IDA + codex cross-analysis of MatroxMGA_reloc):
 * blank (SEQ reset + screen off) -> pixel PLL with lock search -> PAN_CTL ->
 * RAMDAC (skip PLL regs) -> CRTCEXT -> generic VGA (Misc/SEQ/CRTC/GR/ATTR) ->
 * re-latch CRTCEXT0 -> unblank + PAS video-on -> IODelay(10ms) -> clear FB.
 * No transaction/mode-validation layer (the original has none, and the
 * transaction's exact-60Hz timing check rejected real VESA modes).  Each step
 * logs progress so a boot-time hang can be located from the last captured line.
 */
- (BOOL)programLinearMode
{
    const OSMGARes *r = &osmgaRes[selectedResIndex];
    const OSMGAFormat *f = &osmgaFmt[selectedFormatIndex];
    unsigned char crtc[25];
    unsigned char ext[6];
    unsigned char misc;
    vm_address_t base = mmioBase;
    unsigned long mnp = 0;
    unsigned long *fb;
    unsigned long words;
    unsigned long w;
    unsigned int i;
    int locked;

    if (!mmioMapped || !frameBufferMapped) {
        IOLog("OpenStepMGAReplacementDisplay: programLinearMode without mappings\n");
        return NO;
    }
    if (!chipIsG450) {
        IOLog("OpenStepMGAReplacementDisplay: not a G450, refusing mode program\n");
        return NO;
    }

    osmgaComputeCRTC(r, f->bppShift, crtc, ext, &misc);
    IOLog("OpenStepMGAReplacementDisplay: mp %s %s begin\n", r->name, f->cspace);

    /* blank: SEQ async reset asserted + screen off (as MatroxMGA vgaProtect:1) */
    osmgaW8(base, MGA_SEQ_INDEX, 0x00);
    osmgaW8(base, MGA_SEQ_DATA, 0x01);
    osmgaW8(base, MGA_SEQ_INDEX, 0x01);
    osmgaW8(base, MGA_SEQ_DATA, (unsigned char)(osmgaSEQ[1] | 0x20));
    IOLog("OpenStepMGAReplacementDisplay: mp blanked\n");

    /* pixel PLL (clock select is inside osmgaG450SetPLL) + loop filter */
    locked = osmgaG450SetPLL(base, r->clockKHz, &mnp);
    osmgaOutDac(base, MGA_DAC_PAN_CTL, osmgaG450PanCtl(r->clockKHz));
    IOLog("OpenStepMGAReplacementDisplay: mp PLL mnp=%06x locked=%d\n",
          (unsigned int)mnp, locked);
    if (!locked) {
        IOLog("OpenStepMGAReplacementDisplay: mp PLL did NOT lock; revert to VGA\n");
        [super revertToVGAMode];
        return NO;
    }

    /* RAMDAC (skip-filtered; 0x2c-2e/0x4c-4e excluded, PLL already live).
     * DAC 0x19 (MUL_CTL) is the per-format pixel multiplexer. */
    for (i = 0; i < 0x50U; i++)
        if (!osmgaDacSkip(i))
            osmgaOutDac(base, (unsigned char)i,
                        (i == 0x19U) ? f->mulCtl : osmgaInitDAC[i]);
    IOLog("OpenStepMGAReplacementDisplay: mp dac done (mulctl=%02x)\n",
          (unsigned int)f->mulCtl);

    /* MGA CRTC extension */
    for (i = 0; i < 6U; i++)
        osmgaWriteCrtcExt(base, (unsigned char)i, ext[i]);

    /* generic VGA: MiscOut, SEQ2-4 (SEQ1 stays blanked), CRTC (unlock), GR */
    osmgaW8(base, MGA_MISC_WRITE, misc);
    for (i = 2; i < 5U; i++) {
        osmgaW8(base, MGA_SEQ_INDEX, (unsigned char)i);
        osmgaW8(base, MGA_SEQ_DATA, osmgaSEQ[i]);
    }
    osmgaWriteCrtc(base, 0x11, (unsigned char)(osmgaReadCrtc(base, 0x11) & 0x7f));
    for (i = 0; i < 25U; i++)
        osmgaWriteCrtc(base, (unsigned char)i, crtc[i]);
    for (i = 0; i < 9U; i++) {
        osmgaW8(base, MGA_GR_INDEX, (unsigned char)i);
        osmgaW8(base, MGA_GR_DATA, osmgaGR[i]);
    }
    IOLog("OpenStepMGAReplacementDisplay: mp vga+crtc done\n");

    /* attribute controller (PAS off during load) */
    (void)osmgaR8(base, MGA_INSTS1);
    osmgaW8(base, MGA_ATTR_INDEX, 0x00);
    for (i = 0; i < 21U; i++)
        osmgaWriteAttr(base, (unsigned char)i, osmgaAR[i]);

    /*
     * Load a linear (identity) RAMDAC palette.  In 32bpp TrueColor the DAC
     * palette is the per-channel gamma LUT; without loading it, the leftover
     * VGA/boot LUT shows up as a rainbow.  MGA1064 RAMDAC direct registers:
     * PIX_RD_MSK=0x3c02, WADR_PAL=0x3c00, COL_PAL(data)=0x3c01.
     */
    osmgaW8(base, MGA_DAC_INDEX + 2, 0xff);
    osmgaW8(base, MGA_DAC_INDEX, 0x00);
    if (f->isPseudo && paletteValid) {
        /* 8bpp PseudoColor: the window-server colormap (setTransferTable:) */
        for (i = 0; i < 256U; i++) {
            osmgaW8(base, MGA_DAC_INDEX + 1, paletteRed[i]);
            osmgaW8(base, MGA_DAC_INDEX + 1, paletteGreen[i]);
            osmgaW8(base, MGA_DAC_INDEX + 1, paletteBlue[i]);
        }
    } else if (f->ioBpp == IO_15BitsPerPixel) {
        /*
         * 15bpp direct color: the RAMDAC indexes the palette with the raw
         * 5-bit component (0-31), so only 32 entries are live and they must
         * span the full 0-255 output range.  (Loading a 256-entry i->i ramp --
         * as 8/32bpp do -- leaves the max 5-bit value 31 mapped to 31/255 =
         * 12% brightness, i.e. a very dark screen.)  Original MatroxMGA
         * setGammaTable does the same: 32 writes of byte_6984[8*i]; we use a
         * linear 32-step ramp (LUT loading may differ from the original).
         */
        for (i = 0; i < 32U; i++) {
            unsigned char v = (unsigned char)((i * 255U) / 31U);
            osmgaW8(base, MGA_DAC_INDEX + 1, v);
            osmgaW8(base, MGA_DAC_INDEX + 1, v);
            osmgaW8(base, MGA_DAC_INDEX + 1, v);
        }
    } else {
        /* 8bpp/32bpp RGB per-channel gamma AND grayscale: 256-entry ramp.
         * grayLevels>1 quantizes to N evenly spaced output grays (retro look). */
        for (i = 0; i < 256U; i++) {
            unsigned char v;
            if (f->grayLevels > 1) {
                unsigned int lvl = (i * (unsigned int)f->grayLevels) / 256U;
                v = (unsigned char)((lvl * 255U) /
                                    (unsigned int)(f->grayLevels - 1));
            } else {
                v = (unsigned char)i;
            }
            osmgaW8(base, MGA_DAC_INDEX + 1, v);
            osmgaW8(base, MGA_DAC_INDEX + 1, v);
            osmgaW8(base, MGA_DAC_INDEX + 1, v);
        }
    }
    IOLog("OpenStepMGAReplacementDisplay: mp palette loaded (%s)\n",
          (f->isPseudo && paletteValid) ? "colormap" :
          (f->ioBpp == IO_15BitsPerPixel) ? "15bpp-32step" :
          (f->grayLevels > 1) ? "gray-quantized" : "linear");

    /* re-latch CRTCEXT0 (display start) */
    osmgaWriteCrtcExt(base, 0, ext[0]);

    /* unblank: SEQ screen on + reset off, then PAS -> video on (vgaProtect:0) */
    osmgaW8(base, MGA_SEQ_INDEX, 0x01);
    osmgaW8(base, MGA_SEQ_DATA, osmgaSEQ[1]);
    osmgaW8(base, MGA_SEQ_INDEX, 0x00);
    osmgaW8(base, MGA_SEQ_DATA, 0x03);
    (void)osmgaR8(base, MGA_INSTS1);
    osmgaW8(base, MGA_ATTR_INDEX, 0x20);
    IODelay(10000);
    IOLog("OpenStepMGAReplacementDisplay: mp video-on done\n");

    /* clear the visible framebuffer (whole rows, per-format stride) */
    fb = (unsigned long *)[self displayInfo]->frameBuffer;
    words = ((unsigned long)r->width * (unsigned long)f->bytesPerPixel *
             (unsigned long)r->height) / sizeof(unsigned long);
    for (w = 0UL; w < words; w++)
        fb[w] = 0UL;

    linearModeActive = YES;
    IOLog("OpenStepMGAReplacementDisplay: linear mode ACTIVE %s %s\n",
          r->name, f->cspace);
    return YES;
}

/*
 * Set to 1 to program the hardware mode (PLL/CRTC/DAC) in enterLinearMode.
 * Kept 0 for the first activation test: this isolates the framebuffer-mapping
 * fix (mapFrameBufferAtPhysicalAddress) from the mode-programming sequence, so
 * a boot that completes proves the mapping fix without risking a mode-program
 * hang.  Once that is confirmed the mode program is re-enabled.
 */
#define OSMGA_ENABLE_MODE_PROGRAM 1

/*
 * S1: Storm 2D engine liveness test -- docs/S1_STORM_ENGINE_LIVENESS_PLAN.md.
 *
 * Fills one 64x64 OFFSCREEN block with the engine and verifies it by CPU
 * readback.  This is the single unknown the whole 2D/OpenGL effort rests on:
 * does the engine respond to the sequence we derived?
 *
 * Opt-in only ("Storm 2D Test" = "Yes"); a normal boot never writes an engine
 * register.  Runs at the very end of enterLinearMode, i.e. after the display
 * is up and the network is available, so the proven "screen may die but telnet
 * survives" recovery still applies.
 *
 * Containment (plan section 4/5):
 *  - 32bpp only, and only while the block stays inside VRAM proven real by the
 *    working 1600x1200x32 scanout -- populated VRAM has never been measured,
 *    and an address beyond it could alias back into visible scanout.
 *  - CXBNDRY/YTOP/YBOT are narrowed to the block, so the hardware destination
 *    clip contains any coordinate mistake (it cannot contain VRAM aliasing).
 *  - Every wait is bounded; a timeout aborts before EXEC and performs no
 *    cleanup writes to a possibly wedged FIFO.
 *  - Sentinel/readback go through a separate uncached alias mapping, because
 *    the framebuffer mapping's read cache attribute is unproven and `volatile`
 *    does not make cached reads coherent with engine writes.
 */
- (void)runStormLivenessTest
{
    const OSMGARes *r = &osmgaRes[selectedResIndex];
    const OSMGAFormat *f = &osmgaFmt[selectedFormatIndex];
    vm_address_t base = mmioBase;
    unsigned long stridePixels, testY, startPixel, endPixel;
    unsigned long byteStart, byteEnd, mapLen;
    unsigned long checksum, expectChecksum;
    vm_address_t alias = 0;
    volatile unsigned long *blk;
    unsigned int fifoDepth;
    unsigned long row, col;
    unsigned long mismatches = 0UL;
    IOReturn result;

    if (!stormTestEnabled)
        return;
    if (!mmioMapped || !frameBufferMapped || !linearModeActive) {
        IOLog("OpenStepMGA S1: not ready (mmio/fb/mode), skipped\n");
        return;
    }
    if (f->bytesPerPixel != 4) {
        IOLog("OpenStepMGA S1: 32bpp only, current is %s, skipped\n", f->cspace);
        return;
    }

    stridePixels = (unsigned long)r->width;
    testY        = (unsigned long)r->height + OSMGA_S1_GUARD_ROWS;
    startPixel   = testY * stridePixels;
    endPixel     = (testY + OSMGA_S1_H - 1UL) * stridePixels +
                   (OSMGA_S1_X + OSMGA_S1_W - 1UL);
    byteStart    = startPixel * 4UL;
    byteEnd      = (endPixel + 1UL) * 4UL;

    if (byteEnd > OSMGA_S1_VRAM_PROVEN) {
        IOLog("OpenStepMGA S1: block ends at %lu B > proven VRAM %lu B "
              "(populated VRAM unmeasured), skipped\n",
              byteEnd, OSMGA_S1_VRAM_PROVEN);
        return;
    }
    if (byteEnd > MGA_VRAM_16MB) {
        IOLog("OpenStepMGA S1: block outside the 16 MiB mapping, skipped\n");
        return;
    }

    /* Uncached alias of the block, page aligned (plan 3-1). */
    result = osmgaMapUncachedBlock(frameBufferPhysical, byteStart, byteEnd,
                                   &alias, &mapLen, &blk);
    if (result != IO_R_SUCCESS) {
        IOLog("OpenStepMGA S1: uncached alias map failed r=%d, skipped\n",
              (int)result);
        return;
    }

    IOLog("OpenStepMGA S1: begin %dx%d stride=%lu block y=%lu px %lu..%lu "
          "bytes %lu..%lu\n", r->width, r->height, stridePixels, testY,
          startPixel, endPixel, byteStart, byteEnd - 1UL);

    if (!osmgaStormWaitIdle(base)) {
        IOLog("OpenStepMGA S1: engine BUSY at entry (timeout), aborted\n");
        goto unmap;
    }
    fifoDepth = (unsigned int)osmgaR8(base, MGA_FIFOSTATUS);
    IOLog("OpenStepMGA S1: engine idle, fifo depth=%u\n", fifoDepth);

    /* Sentinel through the uncached alias: proves the readback path works and
     * rules out "the memory already held the fill value". */
    for (row = 0UL; row < OSMGA_S1_H; row++)
        for (col = 0UL; col < OSMGA_S1_W; col++)
            blk[row * stridePixels + col] = OSMGA_S1_SENTINEL;
    for (row = 0UL; row < OSMGA_S1_H; row++)
        for (col = 0UL; col < OSMGA_S1_W; col++)
            if (blk[row * stridePixels + col] != OSMGA_S1_SENTINEL)
                mismatches++;
    if (mismatches != 0UL) {
        IOLog("OpenStepMGA S1: sentinel readback failed (%lu bad), aborted\n",
              mismatches);
        goto unmap;
    }
    IOLog("OpenStepMGA S1: sentinel ok\n");

    /* 13 state writes -- gate the whole batch, not just the fill. */
    if (!osmgaStormWaitFifo(base, 13U)) {
        IOLog("OpenStepMGA S1: fifo timeout before init, aborted\n");
        goto unmap;
    }
    osmgaStormInitState(base, stridePixels,
                        OSMGA_S1_X, OSMGA_S1_X + OSMGA_S1_W - 1UL,
                        testY * stridePixels,
                        (testY + OSMGA_S1_H - 1UL) * stridePixels);
    osmgaW32(base, MGA_DWGCTL, MGA_DWGCTL_SOLID_FILL);
    IOLog("OpenStepMGA S1: engine state set (dwgctl=%08lx maccess=%02x)\n",
          MGA_DWGCTL_SOLID_FILL, MGA_MACCESS_PW32);

    if (!osmgaStormWaitFifo(base, 3U)) {
        IOLog("OpenStepMGA S1: fifo timeout before fill, aborted\n");
        goto unmap;
    }
    osmgaW32(base, MGA_FCOL, OSMGA_S1_FILL);
    /* FXBNDRY right edge is exclusive; CXBNDRY above was inclusive. */
    osmgaW32(base, MGA_FXBNDRY,
             ((OSMGA_S1_X + OSMGA_S1_W) << 16) | (OSMGA_S1_X & 0xffffUL));
    /* Writing YDSTLEN+EXEC stores YDSTLEN and triggers the operation. Last. */
    osmgaW32(base, MGA_YDSTLEN + MGA_EXEC, (testY << 16) | OSMGA_S1_H);
    IOLog("OpenStepMGA S1: fill issued\n");

    if (!osmgaStormWaitIdle(base)) {
        IOLog("OpenStepMGA S1: engine did NOT return idle after fill "
              "(timeout) -- FAIL\n");
        goto unmap;
    }

    checksum = 0UL;
    mismatches = 0UL;
    for (row = 0UL; row < OSMGA_S1_H; row++) {
        for (col = 0UL; col < OSMGA_S1_W; col++) {
            unsigned long got = blk[row * stridePixels + col];
            checksum += got;
            if (got != OSMGA_S1_FILL) {
                if (mismatches < 4UL)
                    IOLog("OpenStepMGA S1: mismatch r=%lu c=%lu got=%08lx\n",
                          row, col, got);
                mismatches++;
            }
        }
    }
    expectChecksum = OSMGA_S1_FILL * (OSMGA_S1_W * OSMGA_S1_H);

    if (mismatches == 0UL && checksum == expectChecksum) {
        IOLog("OpenStepMGA S1: PASS -- engine filled %lu px with %08lx, "
              "checksum %08lx\n", OSMGA_S1_W * OSMGA_S1_H, OSMGA_S1_FILL,
              checksum);
    } else {
        IOLog("OpenStepMGA S1: FAIL -- %lu/%lu px wrong, checksum %08lx "
              "expected %08lx\n", mismatches, OSMGA_S1_W * OSMGA_S1_H,
              checksum, expectChecksum);
    }

unmap:
    IOUnmapPhysicalFromIOTask(alias, mapLen);
    IOLog("OpenStepMGA S1: end\n");
}

/*
 * S2: screen-to-screen BITBLT -- docs/S2_STORM_BITBLT_PLAN.md.  This is the
 * presentation primitive: later, Mesa renders into offscreen VRAM and the
 * result reaches the screen through exactly this copy.
 *
 * Split into two phases so the new source-addressing semantics (AR3/AR0/AR5,
 * SGN) and "writing into visible scanout" are never introduced together:
 *   S2a  offscreen -> offscreen   -- all the new semantics, zero display risk
 *   S2b  offscreen -> visible     -- only the destination address is new,
 *                                    and it runs only if S2a passed
 *
 * The source carries a position-encoding pattern, so a shifted or misaligned
 * copy fails the check instead of passing as "something got written".
 */
- (BOOL)runStormBlitOnceFrom:(unsigned long)srcY
                        toX:(unsigned long)dstX
                        toY:(unsigned long)dstY
                     stride:(unsigned long)stridePixels
                      label:(const char *)label
{
    vm_address_t base = mmioBase;
    unsigned long wLast = OSMGA_S1_W - 1UL;
    unsigned long start = srcY * stridePixels + OSMGA_S1_X;   /* YDSTORG = 0 */
    unsigned long end   = start + wLast;                      /* left-to-right */
    unsigned long dstByteStart = (dstY * stridePixels + dstX) * 4UL;
    unsigned long dstByteEnd   =
        ((dstY + OSMGA_S1_H - 1UL) * stridePixels + dstX + OSMGA_S1_W - 1UL)
        * 4UL + 4UL;
    vm_address_t alias = 0;
    unsigned long mapLen = 0;
    volatile unsigned long *blk = 0;
    unsigned long row, col, bad = 0UL;
    IOReturn result;

    /* 12 state writes + DWGCTL; clip narrowed to the DESTINATION only. */
    if (!osmgaStormWaitFifo(base, 13U)) {
        IOLog("OpenStepMGA S2/%s: fifo timeout before init, aborted\n", label);
        return NO;
    }
    osmgaStormInitState(base, stridePixels,
                        dstX, dstX + OSMGA_S1_W - 1UL,
                        dstY * stridePixels,
                        (dstY + OSMGA_S1_H - 1UL) * stridePixels);
    osmgaW32(base, MGA_DWGCTL, MGA_DWGCTL_BITBLT);

    if (!osmgaStormWaitFifo(base, 3U)) {
        IOLog("OpenStepMGA S2/%s: fifo timeout before blit setup, aborted\n",
              label);
        return NO;
    }
    osmgaW32(base, MGA_SGN, MGA_SGN_DOWN_RIGHT);
    osmgaW32(base, MGA_AR5, stridePixels);      /* +1 row, top-down */

    if (!osmgaStormWaitFifo(base, 4U)) {
        IOLog("OpenStepMGA S2/%s: fifo timeout before copy, aborted\n", label);
        return NO;
    }
    osmgaW32(base, MGA_AR0, end);
    osmgaW32(base, MGA_AR3, start);
    /* BITBLT takes an INCLUSIVE right edge (the solid fill takes exclusive). */
    osmgaW32(base, MGA_FXBNDRY, ((dstX + wLast) << 16) | (dstX & 0xffffUL));
    osmgaW32(base, MGA_YDSTLEN + MGA_EXEC, (dstY << 16) | OSMGA_S1_H);

    if (!osmgaStormWaitIdle(base)) {
        IOLog("OpenStepMGA S2/%s: engine did NOT return idle (timeout) -- FAIL\n",
              label);
        return NO;
    }

    result = osmgaMapUncachedBlock(frameBufferPhysical, dstByteStart,
                                   dstByteEnd, &alias, &mapLen, &blk);
    if (result != IO_R_SUCCESS) {
        IOLog("OpenStepMGA S2/%s: dst alias map failed r=%d\n", label,
              (int)result);
        return NO;
    }
    for (row = 0UL; row < OSMGA_S1_H; row++) {
        for (col = 0UL; col < OSMGA_S1_W; col++) {
            unsigned long got = blk[row * stridePixels + col];
            if (got != osmgaS2Pattern(row, col)) {
                if (bad < 4UL)
                    IOLog("OpenStepMGA S2/%s: mismatch r=%lu c=%lu got=%08lx "
                          "want=%08lx\n", label, row, col, got,
                          osmgaS2Pattern(row, col));
                bad++;
            }
        }
    }
    IOUnmapPhysicalFromIOTask(alias, mapLen);

    if (bad == 0UL) {
        IOLog("OpenStepMGA S2/%s: PASS -- %lu px copied to (%lu,%lu)\n",
              label, OSMGA_S1_W * OSMGA_S1_H, dstX, dstY);
        return YES;
    }
    IOLog("OpenStepMGA S2/%s: FAIL -- %lu/%lu px wrong\n", label, bad,
          OSMGA_S1_W * OSMGA_S1_H);
    return NO;
}

- (void)runStormBlitTest
{
    const OSMGARes *r = &osmgaRes[selectedResIndex];
    const OSMGAFormat *f = &osmgaFmt[selectedFormatIndex];
    vm_address_t base = mmioBase;
    unsigned long stridePixels, srcY, dstAY, dstBX, dstBY;
    unsigned long srcByteStart, srcByteEnd, dstAByteEnd, dstBByteEnd;
    vm_address_t alias = 0;
    unsigned long mapLen = 0;
    volatile unsigned long *src = 0;
    unsigned long row, col, bad = 0UL;
    IOReturn result;

    if (!stormTestEnabled)
        return;
    if (!mmioMapped || !frameBufferMapped || !linearModeActive)
        return;
    if (f->bytesPerPixel != 4) {
        IOLog("OpenStepMGA S2: 32bpp only, skipped\n");
        return;
    }

    stridePixels = (unsigned long)r->width;
    srcY  = (unsigned long)r->height + OSMGA_S2_SRC_Y_OFF;
    dstAY = (unsigned long)r->height + OSMGA_S2_DST_Y_OFF;
    dstBX = (unsigned long)r->width  - OSMGA_S1_W;   /* bottom-right corner: */
    dstBY = (unsigned long)r->height - OSMGA_S1_H;   /* overrun runs offscreen */

    srcByteStart = srcY * stridePixels * 4UL;
    srcByteEnd   = ((srcY + OSMGA_S1_H - 1UL) * stridePixels +
                    OSMGA_S1_W - 1UL) * 4UL + 4UL;
    dstAByteEnd  = ((dstAY + OSMGA_S1_H - 1UL) * stridePixels +
                    OSMGA_S1_W - 1UL) * 4UL + 4UL;
    dstBByteEnd  = ((dstBY + OSMGA_S1_H - 1UL) * stridePixels +
                    dstBX + OSMGA_S1_W - 1UL) * 4UL + 4UL;

    /* Everything must sit inside VRAM proven real by the working scanout. */
    if (srcByteEnd > OSMGA_S1_VRAM_PROVEN ||
        dstAByteEnd > OSMGA_S1_VRAM_PROVEN ||
        dstBByteEnd > OSMGA_S1_VRAM_PROVEN) {
        IOLog("OpenStepMGA S2: blocks exceed proven VRAM, skipped\n");
        return;
    }
    /* S2b must land wholly inside the visible image. */
    if (dstBX + OSMGA_S1_W > (unsigned long)r->width ||
        dstBY + OSMGA_S1_H > (unsigned long)r->height) {
        IOLog("OpenStepMGA S2: visible destination out of range, skipped\n");
        return;
    }

    IOLog("OpenStepMGA S2: begin stride=%lu src y=%lu dstA y=%lu "
          "dstB (%lu,%lu)\n", stridePixels, srcY, dstAY, dstBX, dstBY);

    if (!osmgaStormWaitIdle(base)) {
        IOLog("OpenStepMGA S2: engine BUSY at entry (timeout), aborted\n");
        return;
    }

    /* Lay down the position-encoding source pattern through an uncached alias. */
    result = osmgaMapUncachedBlock(frameBufferPhysical, srcByteStart,
                                   srcByteEnd, &alias, &mapLen, &src);
    if (result != IO_R_SUCCESS) {
        IOLog("OpenStepMGA S2: src alias map failed r=%d, skipped\n",
              (int)result);
        return;
    }
    for (row = 0UL; row < OSMGA_S1_H; row++)
        for (col = 0UL; col < OSMGA_S1_W; col++)
            src[row * stridePixels + col] = osmgaS2Pattern(row, col);
    for (row = 0UL; row < OSMGA_S1_H; row++)
        for (col = 0UL; col < OSMGA_S1_W; col++)
            if (src[row * stridePixels + col] != osmgaS2Pattern(row, col))
                bad++;
    IOUnmapPhysicalFromIOTask(alias, mapLen);
    if (bad != 0UL) {
        IOLog("OpenStepMGA S2: source pattern readback failed (%lu bad), "
              "aborted\n", bad);
        return;
    }
    IOLog("OpenStepMGA S2: source pattern ok\n");

    /* S2a: offscreen -> offscreen.  No display risk. */
    if (![self runStormBlitOnceFrom:srcY toX:OSMGA_S1_X toY:dstAY
                             stride:stridePixels label:"a-offscreen"]) {
        IOLog("OpenStepMGA S2: S2a failed -- NOT attempting the visible "
              "destination\n");
        return;
    }

    /* S2b: offscreen -> visible.  Only reached because S2a passed. */
    (void)[self runStormBlitOnceFrom:srcY toX:dstBX toY:dstBY
                              stride:stridePixels label:"b-visible"];
    IOLog("OpenStepMGA S2: end\n");
}

/*
 * S3a self-test: exercise the IODisplayDoBlit path with controlled inputs
 * before any external caller can reach it.  Runs at the end of
 * enterLinearMode, i.e. before the window server paints and before a cursor
 * exists, so cursor/framebuffer concurrency is not yet a factor (that is an
 * S3b concern -- docs/S3_IODISPLAY_DO_BLIT_PLAN.md 2-6).
 *
 * IO_DISPLAY_CAN_BLIT is deliberately NOT advertised, so the window server
 * keeps using its software path this boot.
 */
- (BOOL)stormBlitCheckSrcX:(unsigned)srcX srcY:(unsigned)srcY
                     width:(unsigned)w height:(unsigned)h
                      dstX:(unsigned)dstX dstY:(unsigned)dstY
                     label:(const char *)label
{
    const OSMGARes *r = &osmgaRes[selectedResIndex];
    unsigned long stride = (unsigned long)r->width;
    unsigned long srcBytes = ((unsigned long)srcY * stride + srcX) * 4UL;
    unsigned long srcEnd   = (((unsigned long)(srcY + h - 1U)) * stride +
                              srcX + w - 1U) * 4UL + 4UL;
    unsigned long dstBytes = ((unsigned long)dstY * stride + dstX) * 4UL;
    unsigned long dstEnd   = (((unsigned long)(dstY + h - 1U)) * stride +
                              dstX + w - 1U) * 4UL + 4UL;
    vm_address_t alias = 0;
    unsigned long mapLen = 0;
    volatile unsigned long *p = 0;
    unsigned long row, col, bad = 0UL;
    IOReturn result;

    /* Paint the source with the position-encoding pattern. */
    result = osmgaMapUncachedBlock(frameBufferPhysical, srcBytes, srcEnd,
                                   &alias, &mapLen, &p);
    if (result != IO_R_SUCCESS) {
        IOLog("OpenStepMGA S3/%s: src map failed r=%d\n", label, (int)result);
        return NO;
    }
    for (row = 0UL; row < (unsigned long)h; row++)
        for (col = 0UL; col < (unsigned long)w; col++)
            p[row * stride + col] = osmgaS2Pattern(row, col);
    IOUnmapPhysicalFromIOTask(alias, mapLen);

    result = [self doDisplayBlitSrcX:srcX srcY:srcY width:w height:h
                                dstX:dstX dstY:dstY reason:(unsigned *)0];
    if (result != IO_R_SUCCESS) {
        IOLog("OpenStepMGA S3/%s: blit returned %d -- FAIL\n", label,
              (int)result);
        return NO;
    }

    result = osmgaMapUncachedBlock(frameBufferPhysical, dstBytes, dstEnd,
                                   &alias, &mapLen, &p);
    if (result != IO_R_SUCCESS) {
        IOLog("OpenStepMGA S3/%s: dst map failed r=%d\n", label, (int)result);
        return NO;
    }
    for (row = 0UL; row < (unsigned long)h; row++)
        for (col = 0UL; col < (unsigned long)w; col++)
            if (p[row * stride + col] != osmgaS2Pattern(row, col))
                bad++;
    IOUnmapPhysicalFromIOTask(alias, mapLen);

    if (bad == 0UL) {
        IOLog("OpenStepMGA S3/%s: PASS (%u,%u)->(%u,%u) %ux%u\n",
              label, srcX, srcY, dstX, dstY, w, h);
        return YES;
    }
    IOLog("OpenStepMGA S3/%s: FAIL -- %lu/%lu px wrong\n", label, bad,
          (unsigned long)w * (unsigned long)h);
    return NO;
}

- (void)runStormBlitApiTest
{
    const OSMGARes *r = &osmgaRes[selectedResIndex];
    const OSMGAFormat *f = &osmgaFmt[selectedFormatIndex];
    unsigned W, H;
    IOReturn rr;
    int passes = 0, total = 0;

    if (!stormTestEnabled || stormBlitFailed)
        return;
    if (!mmioMapped || !frameBufferMapped || !linearModeActive)
        return;
    if (f->bytesPerPixel != 4) {
        IOLog("OpenStepMGA S3: 32bpp only, skipped\n");
        return;
    }
    W = (unsigned)r->width;
    H = (unsigned)r->height;
    if (W < 1024U || H < 768U) {
        IOLog("OpenStepMGA S3: self-test geometry needs >=1024x768, skipped\n");
        return;
    }

    /* Accept requests for the duration of the self-test only.  The public
     * capability flag stays clear, so nothing outside the driver calls in. */
    stormBlitReady = YES;
    IOLog("OpenStepMGA S3: self-test begin (CAN_BLIT not advertised)\n");

    total++; if ([self stormBlitCheckSrcX:64  srcY:64  width:64 height:64
                                     dstX:704 dstY:576
                                    label:"a-nonoverlap"]) passes++;
    total++; if ([self stormBlitCheckSrcX:896 srcY:640 width:64 height:64
                                     dstX:896 dstY:672
                                    label:"b-overlap-down"]) passes++;
    total++; if ([self stormBlitCheckSrcX:896 srcY:576 width:64 height:64
                                     dstX:928 dstY:576
                                    label:"c-overlap-right"]) passes++;
    total++; if ([self stormBlitCheckSrcX:880 srcY:432 width:64 height:64
                                     dstX:912 dstY:464
                                    label:"d-overlap-diag"]) passes++;

    /* src == dst must succeed as a no-op without touching the engine. */
    total++;
    rr = [self doDisplayBlitSrcX:704 srcY:576 width:64 height:64
                            dstX:704 dstY:576 reason:(unsigned *)0];
    if (rr == IO_R_SUCCESS) { passes++; IOLog("OpenStepMGA S3/e-noop: PASS\n"); }
    else IOLog("OpenStepMGA S3/e-noop: FAIL rr=%d\n", (int)rr);

    /* Invalid requests must all be refused with IO_R_RESOURCE. */
    total++;
    {
        int refused = 0;
        unsigned *no = (unsigned *)0;
        if ([self doDisplayBlitSrcX:0 srcY:0 width:0 height:64
                       dstX:100 dstY:100 reason:no] == IO_R_RESOURCE) refused++;
        if ([self doDisplayBlitSrcX:0 srcY:0 width:64 height:64
                       dstX:W - 8U dstY:100 reason:no] == IO_R_RESOURCE) refused++;
        if ([self doDisplayBlitSrcX:0 srcY:H - 8U width:64 height:64
                       dstX:0 dstY:0 reason:no] == IO_R_RESOURCE) refused++;
        if ([self doDisplayBlitSrcX:0xFFFFFFF0U srcY:0 width:64 height:64
                       dstX:0 dstY:0 reason:no] == IO_R_RESOURCE) refused++;
        if (refused == 4) { passes++; IOLog("OpenStepMGA S3/f-invalid: PASS "
                                            "(4/4 refused)\n"); }
        else IOLog("OpenStepMGA S3/f-invalid: FAIL (%d/4 refused)\n", refused);
    }

    /* Stay enabled while the config flag is on: the S3b-prep probe client
     * needs to reach OSMGAProbeBlit from userspace. */
    IOLog("OpenStepMGA S3: self-test end %d/%d passed%s\n", passes, total,
          stormBlitFailed ? " (ACCEL PERMANENTLY DISABLED)" : "");
}

/*
 * S3: the documented IODisplayDoBlit acceleration entry point.
 * docs/S3_IODISPLAY_DO_BLIT_PLAN.md.
 *
 * Runs one on-screen rectangle copy on the Storm engine.  Callers reach this
 * through IODeviceMaster's setIntValues:, the same path the window server
 * already uses for brightness and gamma, so userspace never sees MMIO.
 *
 * The contract (driverkit/displayDefs.h) says a driver may return
 * IO_R_RESOURCE and the caller must be prepared to do the copy in software.
 * We use that for every doubt: bad geometry, acceleration not enabled, a
 * concurrent blit already in flight, or a failure before the execute write.
 *
 * The one case that is NOT a soft failure is a timeout AFTER the execute
 * write: the engine may still be writing, so letting the caller redo the
 * copy in software would leave the late engine writes on top.  That path
 * disables acceleration permanently instead.
 */
- (IOReturn)doDisplayBlitSrcX:(unsigned)srcX srcY:(unsigned)srcY
                        width:(unsigned)w height:(unsigned)h
                         dstX:(unsigned)dstX dstY:(unsigned)dstY
                       reason:(unsigned *)outReason
{
    const OSMGARes *r = &osmgaRes[selectedResIndex];
    const OSMGAFormat *f = &osmgaFmt[selectedFormatIndex];
    unsigned long dispW = (unsigned long)r->width;
    unsigned long dispH = (unsigned long)r->height;
    unsigned reason = OSMGA_BLIT_R_GEOMETRY;
    int rc;

    if (stormBlitFailed || !stormBlitReady ||
        !mmioMapped || !frameBufferMapped || !linearModeActive ||
        f->bytesPerPixel != 4) {
        if (outReason) *outReason = OSMGA_BLIT_R_DISABLED;
        return IO_R_RESOURCE;
    }

    /* Parameters arrive as unsigned; a value meant as negative shows up with
     * the high bit set.  Refuse those instead of wrapping into huge extents. */
    if (((srcX | srcY | w | h | dstX | dstY) & 0x80000000U) ||
        w == 0U || h == 0U ||
        /* Both rectangles must lie wholly on the screen (the API is "on the
         * screen"); this also keeps every address inside proven VRAM. */
        (unsigned long)srcX + w > dispW || (unsigned long)srcY + h > dispH ||
        (unsigned long)dstX + w > dispW || (unsigned long)dstY + h > dispH) {
        if (outReason) *outReason = OSMGA_BLIT_R_GEOMETRY;
        return IO_R_RESOURCE;
    }
    if (srcX == dstX && srcY == dstY) {          /* no-op, engine untouched */
        if (outReason) *outReason = OSMGA_BLIT_R_NOOP;
        return IO_R_SUCCESS;
    }

    /* Serialize: refuse rather than wait, so no thread ever spins on another
     * and the caller simply does this one in software.  The cursor code in
     * IOFrameBufferDisplay uses the same philosophy -- it takes a non-blocking
     * ev_try_lock and silently skips when contended (verified by disassembly,
     * docs/S3B_PREP_INSTRUMENTATION_PLAN.md 8-2). */
    simple_lock(&stormLock);
    if (stormBusy) {
        simple_unlock(&stormLock);
        if (outReason) *outReason = OSMGA_BLIT_R_BUSY;
        return IO_R_RESOURCE;
    }
    stormBusy = YES;
    simple_unlock(&stormLock);

    if (!osmgaStormWaitIdle(mmioBase)) {
        rc = 0;                                  /* nothing issued yet */
        reason = OSMGA_BLIT_R_PREEXEC;
    } else {
        rc = osmgaStormBlit(mmioBase, (unsigned long)r->width,
                            (unsigned long)srcX, (unsigned long)srcY,
                            (unsigned long)w, (unsigned long)h,
                            (unsigned long)dstX, (unsigned long)dstY);
        if (rc == 1)       reason = OSMGA_BLIT_R_OK;
        else if (rc == 0)  reason = OSMGA_BLIT_R_PREEXEC;
        else               reason = OSMGA_BLIT_R_POSTEXEC;
    }

    if (rc < 0) {
        /* Post-execute timeout: the engine may still be writing, so the
         * caller must NOT redo this in software.  Disable permanently. */
        stormBlitFailed = YES;
        IOLog("OpenStepMGA S3: execute timed out; acceleration DISABLED "
              "permanently (engine may still be writing)\n");
    }

    simple_lock(&stormLock);
    stormBusy = NO;
    simple_unlock(&stormLock);

    if (outReason) *outReason = reason;
    return (rc == 1) ? IO_R_SUCCESS : IO_R_RESOURCE;
}

/* RPC boundary: count here so the statistics describe EXTERNAL callers, not
 * the boot self-test (which calls the core method directly). */
- (IOReturn)rpcBlitFrom:(unsigned *)p
{
    unsigned reason = OSMGA_BLIT_R_GEOMETRY;
    IOReturn rr;

    statBlitRequests++;
    if (p[2] == 1U || p[3] == 1U)
        statThin1px++;

    /*
     * Reject-only observation: characterise the caller's workload WITHOUT
     * running the engine.  Everything here is arithmetic on the parameters;
     * no MMIO, no framebuffer access, so this cannot corrupt the display.
     * Refusing is the documented contract, so the caller does it in software.
     */
    if (stormObserveOnly) {
        unsigned sx = p[0], sy = p[1], w = p[2], h = p[3], dx = p[4], dy = p[5];
        unsigned cursorTotal = statCursorShow + statCursorMove + statCursorHide;

        statObserved++;
        if (sx < dx) statObsDirLeft++;
        if (sy < dy) statObsDirUp++;
        /* rectangle overlap test */
        if (sx < dx + w && dx < sx + w && sy < dy + h && dy < sy + h)
            statObsOverlap++;
        if (w > statObsMaxW) statObsMaxW = w;
        if (h > statObsMaxH) statObsMaxH = h;
        if (statObsMinDim == 0U || w < statObsMinDim) statObsMinDim = w;
        if (h < statObsMinDim) statObsMinDim = h;
        /* Did the cursor move between the previous request and this one?
         * A high ratio means cursor and blits genuinely interleave. */
        if (cursorTotal != obsLastCursorTotal) {
            statObsCursorNear++;
            obsLastCursorTotal = cursorTotal;
        }
        return IO_R_RESOURCE;          /* recorded, engine untouched */
    }

    rr = [self doDisplayBlitSrcX:p[0] srcY:p[1] width:p[2] height:p[3]
                            dstX:p[4] dstY:p[5] reason:&reason];

    switch (reason) {
    case OSMGA_BLIT_R_OK:       statBlitOk++;           break;
    case OSMGA_BLIT_R_NOOP:     statBlitNoop++;         break;
    case OSMGA_BLIT_R_DISABLED: statRefusedDisabled++;  break;
    case OSMGA_BLIT_R_GEOMETRY: statRefusedGeometry++;  break;
    case OSMGA_BLIT_R_BUSY:     statRefusedBusy++;      break;
    case OSMGA_BLIT_R_PREEXEC:  statRefusedPreExec++;   break;
    case OSMGA_BLIT_R_POSTEXEC: statPostExecTimeout++;  break;
    default: break;
    }
    return rr;
}

/*
 * Cursor instrumentation.  IOFrameBufferDisplay's cursor is a SOFTWARE cursor:
 * disassembly of the kernel shows hideCursor:/showCursor: copying pixels
 * between a private backing store and IODisplayInfo.frameBuffer with the CPU
 * (docs/S3B_PREP_INSTRUMENTATION_PLAN.md 8-1).  So it really can collide with
 * an engine blit over the same rectangle.
 *
 * These overrides only count and forward.  No lock, no allocation, no IOLog,
 * no waiting: the call context is not documented and must be assumed to be
 * interrupt level.
 */
- hideCursor:(int)token
{
    statCursorHide++;
    if (stormBusy) statCursorWhileBusy++;
    return [super hideCursor:token];
}

- moveCursor:(Point *)cursorLoc frame:(int)frame token:(int)t
{
    statCursorMove++;
    if (stormBusy) statCursorWhileBusy++;
    return [super moveCursor:cursorLoc frame:frame token:t];
}

- showCursor:(Point *)cursorLoc frame:(int)frame token:(int)t
{
    statCursorShow++;
    if (stormBusy) statCursorWhileBusy++;
    return [super showCursor:cursorLoc frame:frame token:t];
}

- (IOReturn)getIntValues:(unsigned *)parameterArray
            forParameter:(IOParameterName)parameterName
                   count:(unsigned *)count
{
    if (osmgaTextEquals(parameterName, OSMGA_STATS_PARAM)) {
        /* Exact-size contract, as the DriverKit AMD_SCSI example does: no
         * clamping, no partial snapshot. */
        if (parameterArray == 0 || count == 0 || *count != OSMGA_STATS_COUNT)
            return IO_R_INVALID_ARG;
        parameterArray[0]  = OSMGA_STATS_VERSION;
        parameterArray[1]  = statBlitRequests;
        parameterArray[2]  = statBlitOk;
        parameterArray[3]  = statBlitNoop;
        parameterArray[4]  = statRefusedDisabled;
        parameterArray[5]  = statRefusedGeometry;
        parameterArray[6]  = statRefusedBusy;
        parameterArray[7]  = statRefusedPreExec;
        parameterArray[8]  = statPostExecTimeout;
        parameterArray[9]  = statCursorShow;
        parameterArray[10] = statCursorMove;
        parameterArray[11] = statCursorHide;
        parameterArray[12] = statCursorWhileBusy;
        parameterArray[13] = statThin1px;
        parameterArray[14] = statEnterLinear;
        parameterArray[15] = statRevertVGA;
        parameterArray[16] = stormBlitReady ? 1U : 0U;
        parameterArray[17] = stormBlitFailed ? 1U : 0U;
        parameterArray[18] = stormObserveOnly ? 1U : 0U;
        parameterArray[19] = statObserved;
        parameterArray[20] = statObsOverlap;
        parameterArray[21] = statObsDirUp;
        parameterArray[22] = statObsDirLeft;
        parameterArray[23] = statObsCursorNear;
        parameterArray[24] = (statObsMaxW << 16) | (statObsMaxH & 0xffffU);
        parameterArray[25] = statObsMinDim;
        *count = OSMGA_STATS_COUNT;
        return IO_R_SUCCESS;
    }
    return [super getIntValues:parameterArray
                  forParameter:parameterName
                         count:count];
}

- (IOReturn)setIntValues:(unsigned *)parameterArray
            forParameter:(IOParameterName)parameterName
                   count:(unsigned)count
{
    if (osmgaTextEquals(parameterName, IO_DISPLAY_DO_BLIT) ||
        osmgaTextEquals(parameterName, OSMGA_PROBE_BLIT_PARAM)) {
        if (parameterArray == 0 || count != IO_DISPLAY_BLIT_SIZE) {
            statBlitRequests++;
            statRefusedGeometry++;
            return IO_R_RESOURCE;
        }
        return [self rpcBlitFrom:parameterArray];
    }
    return [super setIntValues:parameterArray
                  forParameter:parameterName
                         count:count];
}

- (void)enterLinearMode
{
    statEnterLinear++;
    IOLog("OpenStepMGAReplacementDisplay: enterLinearMode begin\n");
#if OSMGA_ENABLE_MODE_PROGRAM
    if (![self programLinearMode]) {
        IOLog("OpenStepMGAReplacementDisplay: enterLinearMode FAILED; recover via "
              "R5-VGA config-edit reboot (Active Drivers -> VGA)\n");
        return;
    }
    IOLog("OpenStepMGAReplacementDisplay: enterLinearMode done\n");
    /* Opt-in engine tests, last of all: display is already up and verified.
     * S2 runs after S1 and reuses the same offscreen area for its source. */
    [self runStormLivenessTest];
    [self runStormBlitTest];
    [self runStormBlitApiTest];
#else
    IOLog("OpenStepMGAReplacementDisplay: enterLinearMode mode-program DISABLED "
          "(FB-mapping isolation test); no register programming\n");
#endif
}

/*
 * Load the window-server colormap.  The transfer table packs one 32-bit entry
 * per index: R=bits31-24, G=23-16, B=15-8 for IO_RGBColorSpace; the low byte
 * for IO_OneIsWhiteColorSpace (grayscale).  We cache it in paletteRed/Green/
 * Blue[] and, for 8bpp PseudoColor (RGB:256/8) where the DAC LUT *is* the
 * colormap, push it into the RAMDAC live if the display is already active.
 * TrueColor (RGB:888/32, RGB:555/16) and grayscale (BW:8) keep the fixed linear
 * ramp loaded by programLinearMode, so their transfer table is cached but not
 * applied -- this is the deliberate deviation from the original (which stubbed
 * setTransferTable entirely).
 */
- setTransferTable:(const unsigned int *)table count:(int)count
{
    const OSMGAFormat *f = &osmgaFmt[selectedFormatIndex];
    IOColorSpace cspace = [self displayInfo]->colorSpace;
    int k;

    if (table == 0 || count <= 0)
        return self;

    for (k = 0; k < count && k < 256; k++) {
        if (cspace == IO_OneIsWhiteColorSpace) {
            paletteRed[k] = paletteGreen[k] = paletteBlue[k] =
                (unsigned char)(table[k] & 0xFF);
        } else {
            paletteRed[k]   = (unsigned char)((table[k] >> 24) & 0xFF);
            paletteGreen[k] = (unsigned char)((table[k] >> 16) & 0xFF);
            paletteBlue[k]  = (unsigned char)((table[k] >> 8) & 0xFF);
        }
    }
    /* Zero-fill any entries the server did not supply. */
    for (; k < 256; k++)
        paletteRed[k] = paletteGreen[k] = paletteBlue[k] = 0;
    paletteValid = YES;

    if (linearModeActive && f->isPseudo && mmioMapped) {
        vm_address_t base = mmioBase;
        osmgaW8(base, MGA_DAC_INDEX + 2, 0xff);
        osmgaW8(base, MGA_DAC_INDEX, 0x00);
        for (k = 0; k < 256; k++) {
            osmgaW8(base, MGA_DAC_INDEX + 1, paletteRed[k]);
            osmgaW8(base, MGA_DAC_INDEX + 1, paletteGreen[k]);
            osmgaW8(base, MGA_DAC_INDEX + 1, paletteBlue[k]);
        }
        IOLog("OpenStepMGAReplacementDisplay: setTransferTable live colormap "
              "(%d entries)\n", count);
    } else {
        IOLog("OpenStepMGAReplacementDisplay: setTransferTable cached "
              "(%d entries, %s)\n", count, f->cspace);
    }
    return self;
}

- (void)revertToVGAMode
{
    statRevertVGA++;
    linearModeActive = NO;
    [super revertToVGAMode];
}

- (unsigned int)displayMemorySize
{
    return (unsigned int)MGA_VRAM_16MB;
}

- (unsigned int)ramdacSpeed
{
    return 300000000U;   /* H1 family envelope, matches R2 profile input */
}

- setBrightness:(int)level token:(int)token
{
    (void)level;
    (void)token;
    return self;
}

- free
{
    [self teardownMappings];
    linearModeActive = NO;
    manualVideoMemoryConfigured = NO;
    configuredVideoMemoryBytes = 0;
    return [super free];
}

@end
