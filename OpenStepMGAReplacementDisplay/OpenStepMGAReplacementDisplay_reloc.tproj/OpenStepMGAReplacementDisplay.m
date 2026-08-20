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
 * init maps BAR0 (framebuffer, via mapFrameBufferAtPhysicalAddress:length:)
 * and BAR1 (MMIO, via
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
#import <driverkit/i386/kernelDriver.h>   /* IOMallocLow / IOFreeLow */
#import <driverkit/devsw.h>
#import <mach/vm_param.h>
#import <bsd/sys/mman.h>
#import <bsd/sys/errno.h>
#import "OpenStepMGAReplacementDisplay.h"
#import "OpenStepMGAManualConfig.h"
#import "OpenStepMGAEDID.h"
#import "OpenStepMGAWarpUcode.h"
#import "OpenStepMGAHW3D.h"

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
#define OSMGA_S1_FIFO_MAX       16U   /* the DDX's own largest ask */
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
#define OSMGA_STATS_COUNT       19
/*
 * displayDefs.h says callers must not use IO_DISPLAY_DO_BLIT unless
 * IO_DISPLAY_CAN_BLIT is set.  We have not set it, so our own probe client
 * uses this private parameter instead of violating that contract.  Same six
 * arguments, same validation, same engine helper.
 */
#define OSMGA_PROBE_BLIT_PARAM  "OSMGAProbeBlit"
/* Solid fill inside the mmap window; lets a client prove engine/mapping
 * coherence.  Parameters: [x, y, w, h, colour]. */
#define OSMGA_PROBE_FILL_PARAM  "OSMGAProbeFill"

/* Single primary outcome per request, for the statistics. */
#define OSMGA_BLIT_R_OK         0
#define OSMGA_BLIT_R_NOOP       1
#define OSMGA_BLIT_R_DISABLED   2
#define OSMGA_BLIT_R_GEOMETRY   3
#define OSMGA_BLIT_R_BUSY       4
#define OSMGA_BLIT_R_PREEXEC    5
#define OSMGA_BLIT_R_POSTEXEC   6

/*
 * ---- S4a: offscreen VRAM mapped into a user task (cdevsw mmap) ----
 * docs/S4A_VRAM_MMAP_PLAN.md.
 *
 * Contract established by disassembling this target's mach_kernel:
 *   d_mmap(dev, offset, prot) is cdevsw entry index 8; it returns a PAGE FRAME
 *   NUMBER and refuses with -1.  It is called AT LEAST TWICE for the same
 *   offset -- once in _smmap's validation loop and again in vm_object_special
 *   to populate the object -- and the second call is NOT checked for -1: that
 *   path does `*slot = ret << PAGE_SHIFT` unconditionally.  A handler that
 *   passes validation and then refuses would hand the kernel 0xFFFFF000 as a
 *   physical address.
 *
 * Therefore the handler is strictly deterministic over IMMUTABLE state.  The
 * feature is gated by not registering the device at all, never by a runtime
 * flag in the decision path.  A non-(-1) return is trusted by the kernel with
 * no validation of its own, so a wrong PFN would map arbitrary physical RAM,
 * MMIO or nonexistent space into a user process -- correctness here is
 * entirely ours.
 *
 * dev encoding is derived from the same disassembly: _smmap indexes cdevsw
 * with the byte at offset 67 of a 16-bit dev word whose low byte is at 66, so
 * the high byte is the major and the low byte is the minor.
 */
#define OSMGA_DEV_MINOR(d)      ((d) & 0xFF)
#define OSMGA_PROT_RW           (PROT_READ | PROT_WRITE)
/* Offscreen window: guard rows below the visible image, up to proven VRAM. */
#define OSMGA_MMAP_GUARD_ROWS   256UL

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
#define OSMGA_S1_VRAM_BLOCK     (4UL * 1024UL * 1024UL)   /* offscreen test block */

/*
 * D1-0 -- primary DMA ring memory (docs/D1_PRIMARY_DMA_RING_PLAN.md).
 *
 * IOMallocLow is the only physically contiguous allocator this kernel has,
 * and it caps a single request at 64 KiB: dma_buf_alloc (0x18980c) refuses
 * anything larger, and its pools are carved by alloc_cnvmem (0x18ad9c), a
 * bump allocator over the conventional-memory arena.  That is why the ring
 * is exactly 64 KiB and why the address is expected below 0xA0000.
 *
 * This step allocates and measures.  It writes no engine register, so a
 * failure here costs nothing but a log line.
 */
#define OSMGA_DMA_RING_BYTES    0x10000UL
#define OSMGA_CONVENTIONAL_END  0xA0000UL

/*
 * Primary DMA registers (legacy MGA DRM mga_drv.h, MIT).  These are MMIO
 * only -- none of them is inside a DMA-addressable window, which is the
 * point: the list cannot reprogram the engine that reads the list.
 */
#define MGA_PRIMADDRESS         0x1e58
#define MGA_PRIMEND             0x1e5c
#define MGA_PRIMPTR             0x1e50   /* pointer writeback enable */
#define MGA_SECADDRESS          0x2c40
#define MGA_SECEND              0x2c44
#define MGA_SETUPADDRESS        0x2cd0
#define MGA_SETUPEND            0x2cd4
#define MGA_SOFTRAP             0x2c48
#define MGA_DMAPAD              0x1c54
#define MGA_ICLEAR              0x1e18
#define MGA_SOFTRAPICLR         0x00000001UL
#define MGA_STATUS_SOFTRAPEN    0x00000001UL   /* STATUS bit0  */
#define MGA_STATUS_DWGENGSTS    0x00010000UL   /* STATUS bit16 */
#define MGA_STATUS_ENDPRDMASTS  0x00020000UL   /* STATUS bit17 */
/*
 * Completion of a polled primary-DMA list is three conditions at once:
 * the trap we put at the end of the list has fired, the drawing engine has
 * gone idle, and the DMA read pointer has reached the end.
 *
 * Note SOFTRAPEN is set on completion, not cleared.  The DRM compares
 * (status & ENGINE_IDLE_MASK) == ENDPRDMASTS, which requires SOFTRAPEN to
 * be *clear* -- that works there because its interrupt handler clears the
 * trap through ICLEAR.  Polling code that copies the comparison waits for
 * a condition its own completion signal prevents; measured on hardware as
 * STATUS=80820025, a finished transfer that never satisfied the test.
 */
#define MGA_DMA_DONE_MASK       (MGA_STATUS_SOFTRAPEN | \
                                 MGA_STATUS_DWGENGSTS | \
                                 MGA_STATUS_ENDPRDMASTS)
#define MGA_DMA_DONE_VALUE      (MGA_STATUS_SOFTRAPEN | \
                                 MGA_STATUS_ENDPRDMASTS)
/*
 * WARP registers (legacy MGA DRM mga_drv.h, MIT; X.Org mga_reg.h agrees).
 *
 * WIADDR2's low two bits are the pipe mode, and a pipe runs by writing
 * WIADDR2 = microcode_phys | WMODE_START.  That is why this code never
 * restores a previously-read WIADDR2: restoring a value that happens to
 * end in 3 would start the WARP pipe at an address nobody chose.  The end
 * state is the known one -- suspended -- not the prior one.
 */
#define MGA_WIADDR              0x1dc0
#define MGA_WGETMSB             0x1dc8
#define MGA_WVRTXSZ             0x1dcc
#define MGA_WACCEPTSEQ          0x1dd4
#define MGA_WIADDR2             0x1dd8
#define MGA_WMODE_SUSPEND       0x00000000UL
#define MGA_WMISC               0x1e70   /* MMIO only, like OPMODE */
#define MGA_WUCODECACHE_ENABLE  0x00000001UL
#define MGA_WMASTER_ENABLE      0x00000002UL
#define MGA_WCACHEFLUSH_ENABLE  0x00000008UL
/* Written: all three.  Read back: bit 3 is not expected to survive -- this
 * is the DRM's own pass/fail test, not a criterion of ours. */
#define MGA_WMISC_WRITE         (MGA_WUCODECACHE_ENABLE | \
                                 MGA_WMASTER_ENABLE | \
                                 MGA_WCACHEFLUSH_ENABLE)
#define MGA_WMISC_EXPECTED      (MGA_WUCODECACHE_ENABLE | MGA_WMASTER_ENABLE)

/*
 * The G400 init values, straight from mga_warp_init.  The DRM's own
 * comment on them is "FIXME: Get rid of these damned magic numbers", so
 * their derivation is not documented anywhere we can read; they are used
 * because that is what shipped, not because we understand them.
 */
#define MGA_WGETMSB_G400        0x00000E00UL
#define MGA_WVRTXSZ_G400        0x00001807UL
#define MGA_WACCEPTSEQ_G400     0x18000000UL

/* Pipe emit registers and their magic values (mga_state.c, mga_drv.h). */
#define MGA_WFLAG               0x1dc4
#define MGA_WFLAG1              0x1de0
#define MGA_WR49                0x2dc4
#define MGA_WR52                0x2dd0
#define MGA_WR53                0x2dd4
#define MGA_WR54                0x2dd8
#define MGA_WR56                0x2de0
#define MGA_WR57                0x2de4
#define MGA_WR60                0x2df0
#define MGA_WR61                0x2df4
#define MGA_WR62                0x2df8
#define MGA_G400_WR_MAGIC       0x00000040UL   /* 1 << 6 */
#define MGA_G400_WR56_MAGIC     0x46480000UL
#define MGA_WMODE_START         0x00000003UL

/*
 * D3 -- the Storm engine's own interpolating rasteriser (mga_reg.h).
 *
 * DWGCTL atype selects it: RPL (0<<4) is the replace mode S1 used, I
 * (7<<4) interpolates, ZI (3<<4) interpolates with Z.  DR4/DR8/DR12 hold
 * the red/green/blue start values and the neighbouring registers their
 * increments; ALPHASTART and the TMR/TEX group extend the same mechanism
 * to alpha and texture.
 *
 * The Gouraud DWGCTL below is S1's solid fill with SOLID removed and the
 * atype changed -- everything else, including the geometry registers,
 * stays as S1 proved it.  Note that no source we have uses TRAP | I; what
 * they use is TEXTURE_TRAP | I, for X Render acceleration.  So this
 * combination is an experiment, and its failure would first mean "this
 * combination is unsupported", not "interpolation does not work".
 */
#define MGA_DR4                 0x1cd0   /* red   start */
#define MGA_DR6                 0x1cd8   /* red   increment a */
#define MGA_DR7                 0x1cdc   /* red   increment b */
#define MGA_DR8                 0x1ce0   /* green start */
#define MGA_DR10                0x1ce8   /* green increment a */
#define MGA_DR11                0x1cec   /* green increment b */
#define MGA_DR12                0x1cf0   /* blue  start */
#define MGA_DR14                0x1cf8   /* blue  increment a */
#define MGA_DR15                0x1cfc   /* blue  increment b */
#define MGA_DWGCTL_ATYPE_I      0x00000070UL   /* 7 << 4 */
#define MGA_DWGCTL_SOLID        0x00000800UL   /* 1 << 11 */
#define MGA_DWGCTL_GOURAUD      ((MGA_DWGCTL_SOLID_FILL & \
                                  ~MGA_DWGCTL_SOLID) | MGA_DWGCTL_ATYPE_I)

/* mga_storm.c writes colour start values as (component << 7); the rest of
 * the fixed-point layout is not stated anywhere we can read, so the probe
 * measures it instead of assuming it. */
#define MGA_DR_SHIFT            7

/*
 * D3-2 -- sloped trapezoid edges.
 *
 * The AR registers walk both edges once ARZERO and SGNZERO are cleared;
 * with them set the engine ignores AR/SGN entirely, which is why every
 * probe so far produced an axis-aligned rectangle.  The recipe is X.Org
 * XAA's mgaSubsequentSolidFillTrap, corroborated by a second independent
 * implementation in the same file (mgaSubsequentPatternFillTrap) writing
 * the identical registers.
 *
 * AR3 is deliberately not written.  It is a BITBLT/ILOAD source-address
 * register: the DDX's trapezoid path never touches it, the shipping
 * Windows HAL uses it only in blits, and both interleave blits and
 * trapezoids without clearing it in between.
 *
 * Containment note: with ARZERO clear, FXBNDRY and YDSTLEN are the edge
 * seed and the row count, not a bounding rectangle.  A wrong edge walks
 * out of them.  The real guard is CXBNDRY/YTOP/YBOT, which
 * osmgaStormInitState narrows before every draw -- and whether that guard
 * holds for sloped spans has never been measured, which is why this probe
 * insets x and checks guard columns.
 */
#define MGA_AR1                 0x1c64
#define MGA_AR2                 0x1c68
#define MGA_AR4                 0x1c70
#define MGA_AR6                 0x1c78
#define MGA_DWGCTL_ARZERO       0x00001000UL
#define MGA_DWGCTL_SGNZERO      0x00002000UL
#define MGA_DWGCTL_SLOPED(base) ((base) & ~(MGA_DWGCTL_ARZERO | \
                                            MGA_DWGCTL_SGNZERO))

/* Probe geometry: inset from the block edge so a runaway edge has
 * somewhere to be caught, with guard columns either side. */
#define OSMGA_D3_INSET          8UL
#define OSMGA_D3_WIDTH          48UL
#define OSMGA_D3_BAND           20UL

/*
 * D3-3a-0 -- what does DSTORG mean?
 *
 * Every draw in this driver so far has left DSTORG at zero and reached
 * offscreen memory by using a large y.  That cannot work for depth: if Z
 * is addressed as ZORG + y*pitch + x, then at y=1024 the depth access
 * lands 4 MiB above ZORG, which would force ZORG down into the visible
 * framebuffer to stay inside proven VRAM.  The normal arrangement is
 * DSTORG at the colour origin and ZORG at the depth origin with small y,
 * so DSTORG has to be understood first.
 *
 * The probe writes four rows at y=0 with DSTORG pointed at the offscreen
 * block and then reads both places.  If DSTORG is a pixel offset rather
 * than a byte offset the write can wrap into the visible area -- no value
 * is safe under both readings -- so it is kept to four rows, which is the
 * same order of visible artefact S2b already makes every boot.
 */
#define OSMGA_D3_DSTORG_TEST    OSMGA_S1_VRAM_BLOCK
#define OSMGA_D3_DSTORG_ROWS    4UL

/*
 * D3-3a -- does the Z compare work?
 *
 * Colour goes to the offscreen block through DSTORG and depth to its own
 * area through ZORG, both with small y, which D3-3a-0 showed is how these
 * origins are meant to be used.
 *
 * The depth clear value is chosen to read as mid-scale whether depth is
 * 16 or 32 bits wide -- nothing in the sources says which, and MACCESS's
 * pixel width may or may not cover it.  0x80008000 is mid either way: as
 * one 32-bit word, or as two 16-bit entries of 0x8000.
 *
 * The two ZLT bands then use the extreme values, so the outcome does not
 * depend on knowing the Z scale in DR0 either: zero is below mid-scale
 * under any reading, and all-ones is above it.
 */
#define MGA_ZORG                0x1c0c
#define MGA_DWGCTL_ATYPE_ZI     0x00000030UL   /* 3 << 4 */
#define MGA_DWGCTL_NOZCMP       0x00000000UL   /* 0 << 8 */
#define MGA_DWGCTL_ZLT          0x00000400UL   /* 4 << 8 */
#define MGA_DR0                 0x1cc0         /* Z start, by inference */
#define MGA_DR2                 0x1cc8         /* Z x increment */
#define MGA_DR3                 0x1ccc         /* Z y increment */
#define OSMGA_D3_ZORG           (5UL * 1024UL * 1024UL)
#define OSMGA_D3_ZCLEAR         0x80008000UL

/*
 * D3-3a-1 -- why did the colour vanish?
 *
 * D3-3a drew nothing at all, not even its NOZCMP control band, yet the
 * depth area changed.  The engine ran and wrote depth but no colour, so
 * the colour path was misconfigured rather than the command ignored.
 *
 * Five bands turn one variable off at a time.  atype ZI has no precedent
 * to copy -- MGADWG_ZI, MGA_ATYPE_ZI and DC_atype_zi are all defined and
 * never used, and the DDX ROP tables carry only RPL/RSTR/BLK -- so the
 * hypothesis that ZI is the Z-buffered form of atype I, and therefore
 * needs SOLID cleared and the colour interpolators loaded exactly as I
 * does, can only be settled by measurement.  Bands D and E separate the
 * two halves of that hypothesis; without D they would stay welded.
 *
 * ZORG is programmed for every band including the atype RPL baselines,
 * which the plan did not ask for.  Leaving it at zero would aim any
 * unexpected depth write at the visible framebuffer, and RPL not touching
 * depth is an assumption, not something we have measured.  The cost is
 * that a broken ZORG would take the baseline down with it -- which V1
 * still reports, just attributed one step earlier.
 *
 * Depth is 16-bit: MACCESS bits 3-4 are a depth-specific width whose zero
 * value is MA_zwidth_16, and we only ever write the pixel-width field.
 * There is no depth pitch register, so a depth row is PITCH elements of
 * two bytes.  The readback below uses that layout.
 */
/*
 * D3-3b -- the DR0 to depth encoding, and whether the compare engages.
 *
 * D3-3a reported a failure that was not one.  It counted destination
 * pixels equal to FCOL, but atype ZI does not take colour from FCOL even
 * with SOLID set, so every drawn pixel went uncounted; and it cleared the
 * depth buffer through the colour buffer's geometry, which at a 2048-byte
 * depth row covers only alternate rows' first 256 bytes, leaving the rest
 * of the depth buffer holding whatever was there.  Both bugs came from
 * writing a depth probe before the depth layout was known.
 *
 * So this probe counts pixels that changed rather than pixels matching a
 * colour, reports the values it actually found, and addresses depth as
 * 16-bit elements PITCH apart.
 *
 * The bit walk is the instrument that settled the colour interpolator
 * format in one boot after four wrong guesses: one band per DR0 bit, read
 * the depth value back, and the bit correspondence falls out.
 */
#define OSMGA_D3_ISO_BANDS      5UL
#define OSMGA_D3_ISO_GUARDROWS  4UL
#define OSMGA_D3_ZSENTINEL      0x8000U
#define OSMGA_D3_WALK_ROWS      2UL
#define OSMGA_D3_CMP_ROWS       8UL
#define OSMGA_D3_CMP_BLUE       (200UL << 15)

/*
 * D3-3c -- the compare, with values inside the range the bit walk found.
 *
 * D3-3b measured DR0 = Z << 15: DR0 bit 15 lands on depth bit 0 and the
 * correspondence runs one-to-one up to DR0 bit 30, the same fixed-point
 * position the colour interpolators use.  Only bit 31 is odd, and its
 * raw value is reported here rather than left as a loose end.
 *
 * That makes D3-3b's compare result a bad test rather than a hardware
 * fault: 0xFFFFFFFF is not a far depth, it reached the buffer as 0, so
 * both ZLT bands were testing the same near value.  The depth the engine
 * left behind proves the comparator accepted rather than ignored them --
 * a rejected pixel would have left the clear value untouched.
 *
 * So the bands below use depths that sit in the linear range, one on
 * each side of the cleared 0x8000, plus a reversed-sense band: if ZLT
 * rejects the far depth and ZGTE accepts it, the comparator is working
 * and its polarity is settled at the same time.
 */
#define OSMGA_D3_ZMID           0x40000000UL   /* depth 0x8000 */
#define OSMGA_D3_ZNEAR          0x20000000UL   /* depth 0x4000 */
#define OSMGA_D3_ZFAR           0x60000000UL   /* depth 0xC000 */
#define MGA_DWGCTL_ZGTE         0x00000700UL   /* 7 << 8 */

/*
 * D3-4a -- texture mapping.
 *
 * Unlike atype ZI, which no code anywhere uses, the texture unit has two
 * working implementations in the DDX, and mga_exa.c reaches it with no
 * WARP register references at all.  EXA runs without DRI, so texturing
 * without WARP is shipped behaviour rather than our inference.
 *
 * The command is one bit from what we already run: TEXTURE_TRAP replaces
 * TRAP in the Gouraud word, atype stays I, SOLID stays clear.
 *
 * The coordinate scale is the part that is easy to get wrong.
 * setTMIncrementsRegs computes decal = mga_fx_width_size - 16, and the
 * caller passes 20 - log2(width), so decal = 4 - log2(width) -- negative
 * for any texture wider than 16 texels, meaning a right shift.  One
 * texel step is therefore 1 << (20 - log2(width)), which is 0x4000 for
 * 64 wide.  A 16-texel texture makes decal zero and leaves 16.16
 * untouched, which is the check that settles the sign.
 *
 * Containment differs from the depth work: the texture unit only reads,
 * so a wrong coordinate reads the wrong VRAM rather than writing outside
 * it.  The single write is the destination, bounded by the clip we
 * measured.  CLAMPUV is set but not leaned on -- whether it clamps
 * before or after the address computation is unverified.
 */
#define MGA_TMR0                0x2c00
#define MGA_TMR3                0x2c0c
#define MGA_TMR8                0x2c20
#define MGA_TEXORG              0x2c24
#define MGA_TEXWIDTH            0x2c28
#define MGA_TEXHEIGHT           0x2c2c
#define MGA_TEXCTL              0x2c30
#define MGA_TEXTRANS            0x2c34
#define MGA_TEXTRANSHIGH        0x2c38
#define MGA_TEXCTL2             0x2c3c
#define MGA_TEXFILTER           0x2c58
#define MGA_ALPHACTRL           0x2c7c
#define MGA_TDUALSTAGE0         0x2cf8
#define MGA_TDUALSTAGE1         0x2cfc

#define MGA_DWGCTL_OPCODE_MASK  0x0000000FUL
#define MGA_DWGCTL_TEXTURE_TRAP 0x00000006UL
#define MGA_TEXCTL_PITCHLIN     0x00000100UL
#define MGA_TEXCTL_NOPERSP      0x00200000UL
#define MGA_TEXCTL_TAKEY        0x02000000UL
#define MGA_TEXCTL_CLAMPUV      0x18000000UL
#define MGA_TEXCTL_TW32         0x00000006UL
#define MGA_TEXCTL2_G400_MAGIC  0x00008000UL
#define MGA_TEXCTL2_CKSTRANSDIS 0x00000010UL
#define MGA_TEXFILTER_ALPHA     0x00100000UL
#define MGA_ALPHACTRL_OPAQUE    0x00000101UL   /* ALPHACHANNEL|SRC_ONE|DST_ZERO */

#define OSMGA_D3_TEXORG         (6UL * 1024UL * 1024UL)
#define OSMGA_D3_TEXDIM         64UL
#define OSMGA_D3_TEXLOG2        6UL

/*
 * D3-4b -- scale, sloped edges, and depth under sloped edges.
 *
 * Two questions here have no answer anywhere in the sources, because no
 * shipped path draws a textured sloped trapezoid at all.
 *
 * The first is where the texture coordinate comes from on a sloped edge.
 * If the engine evaluates it from the pixel's own position then a sloped
 * left edge changes nothing; if it accumulates along each span from that
 * span's first pixel, the texture shears and the left edge always shows
 * u = 0.  Band C slopes the LEFT edge and reads the texel at each row's
 * first drawn pixel, which separates the two in one number per row.
 *
 * The second is the coordinate origin.  mga_storm.c:561-563 writes TMR6
 * and TMR7 as the source origin and puts the destination position only
 * in FXBNDRY and YDSTLEN, so the origin should be the primitive rather
 * than the screen -- but D3-4a drew at y = 0, where the two agree.
 * Band B starts at y = 20 with TMR7 = 0, so the v it reports says which.
 *
 * Band D asks whether the depth write follows the sloped edges or covers
 * the whole clip rectangle.  It counts depth inside the band rows but
 * outside the drawn span, which is sharper than an outer guard: depth
 * that ignored the edge walk would land exactly there.
 */
#define OSMGA_D3_4B_BAND        20UL
#define OSMGA_D3_4B_SLOPE       40L

/*
 * D3-4c -- turn three "consistent with" readings into measurements.
 *
 * D3-4b left three conclusions resting on samples that did not fully
 * separate the alternatives, which the cross-review was right to press
 * on even though its own counter-model for the third one described a
 * driver behaviour we never performed.
 *
 * Band E samples a FIXED x across rows of a sloped shape.  D3-4b only
 * ever looked at each row's first drawn pixel, so it excluded the model
 * where the engine restarts the coordinate at every span, but not the
 * weaker claim that u depends on x alone.  If a column has one u all the
 * way down, it does.
 *
 * Band F makes the clip taller than the primitive.  In D3-4b the two
 * began at the same row, so "origin at the primitive" and "origin at the
 * clip rectangle" predicted the same v and the run could not choose.
 *
 * Band G fills the VRAM after the texture with a value no texel can hold
 * and then magnifies far past the texture's edge.  A clamped COORDINATE
 * is what D3-4b measured; whether the ADDRESS is clamped with it is a
 * different question, and it is the one our safety argument leans on.
 */
#define OSMGA_D3_CANARY         0xCAFE0000UL
#define OSMGA_D3_CANARY_BYTES   (512UL * 1024UL)

/*
 * D3-5a -- where does the source alpha come from, and in what format?
 *
 * Every shipped ALPHACTRL recipe is a texture path, so the alpha comes
 * from a texture's alpha channel.  Mesa's commonest blend is textureless
 * per-vertex alpha, which has to arrive through the ALPHASTART
 * interpolator instead, and MGA_DIFFUSEDALPHA reads like the bit that
 * selects it -- but nothing in either tree ever writes that bit.  Same
 * situation as atype ZI and DR0, both of which worked once measured.
 *
 * The trick that makes this cheap: draw with SRC_ALPHA and DST_ZERO from
 * a pure white source, and the result is 255 * alpha / 255, so the pixel
 * read back IS the alpha.  No separate readback path is needed, and a
 * pre-multiplied pipeline would show up as alpha squared, which the
 * bit-walk map reports as impure bits rather than a clean power of two.
 *
 * Bands 34..37 are a two-by-two of DIFFUSEDALPHA against the extremes of
 * ALPHASTART.  Toggling the bit alone cannot distinguish "the bit is not
 * needed" from "alpha arrived as zero from somewhere else"; watching
 * whether the output follows the register in each state can.
 */
#define MGA_ALPHASTART          0x2c70
#define MGA_ALPHAXINC           0x2c74
#define MGA_ALPHAYINC           0x2c78
#define MGA_ALPHA_SRC_ONE       0x00000001UL
#define MGA_ALPHA_SRC_ALPHA     0x00000004UL
#define MGA_ALPHA_DST_ZERO      0x00000000UL
#define MGA_ALPHA_CHANNEL       0x00000100UL
#define MGA_ALPHA_DIFFUSED      0x01000000UL
#define OSMGA_D3_ALPHA_BANDS    38UL
#define OSMGA_D3_ALPHA_ROWS     2UL

/*
 * D3-5b -- linearity, the alpha source, and which origin the blend reads.
 *
 * D3-5a settled the format (ALPHASTART = alpha << 15, eight bits,
 * saturating above bit 22) but the cross-review was right that two of its
 * conclusions rested on samples that could not carry them.
 *
 * A one-hot walk proves eight basis vectors, not linearity: a function
 * like 1 << min(msb - 15, 7) reproduces the same table.  Mixed-bit alphas
 * separate them, since only a linear path returns the bits it was given.
 *
 * And the DIFFUSEDALPHA comparison used only 0 and 255, exactly the two
 * values at which a different alpha source would agree by accident -- a
 * white source carries 255 in its own colour channels.  The walk itself
 * ran with the bit SET throughout, so the cleared case had no interior
 * evidence at all.  Here both states get the same six interior alphas.
 *
 * The second half then blends over a known destination.  SRCORG points at
 * a third offscreen block holding a different colour, so the result names
 * which origin the read followed.  Our shared engine state leaves SRCORG
 * at zero, which would aim that read at the visible framebuffer, and no
 * source settles the question: EXA's composite path sets DSTORG and never
 * SRCORG, and the DRM context has no SRCORG field at all.
 */
#define MGA_ALPHA_DST_1MSA      0x00000050UL   /* 5 << 4 */
#define OSMGA_D3_BLEND_SRCORG   (5UL * 1024UL * 1024UL + 512UL * 1024UL)
#define OSMGA_D3_BLEND_DSTVAL   0x00204060UL
#define OSMGA_D3_BLEND_SRCVAL   0x00807060UL
#define OSMGA_D3_BLEND_COLOUR   0x00C0A080UL
#define OSMGA_D3_BLEND_ROWS     4UL

/*
 * ---- M1-2c: push the containment, not the draw ----
 *
 * The validator bounds the origins and the row and column extents, but it
 * does not bound the AR edge slopes at all -- a batch may ask for an edge
 * that walks far outside the window.  The whole containment argument then
 * rests on the hardware clip, and that has only ever been exercised with
 * geometry that stays inside it.  This asks what happens when it does not.
 *
 * Four bands that all start at the clip edge would be vacuous: an edge
 * walking outward and an edge ignored entirely both produce the full
 * rectangle, so the result would be consistent with the AR machinery
 * never having run.  Bands 4 and 5 start INSIDE the clip and walk out,
 * which separates them -- engaged gives a narrow first row and full rows
 * after, ignored gives a narrow row every time (1248 against 640,
 * scratchpad/m1_2c.py).
 *
 * The slope magnitudes are deliberately modest.  Clipping clamps the span
 * to CXLEFT..CXRIGHT per pixel, so if it works at all it works regardless
 * of how far the unclipped edge would have wandered; a larger slope tells
 * us nothing more.  It does, however, decide where an ESCAPE would land,
 * and at 2^17 per row the unclipped address goes negative and reaches the
 * visible framebuffer.  These values keep even an unclipped walk inside
 * the canary, which is the difference between a measurement and a gamble.
 */
#define OSMGA_M1C_DSTORG        (5UL * 1024UL * 1024UL)
#define OSMGA_M1C_MARGIN_ROWS   64UL
#define OSMGA_M1C_BAND          20UL
#define OSMGA_M1C_SLOPE         800L
#define OSMGA_M1C_CANARY        0xC0DEC0DEUL

/*
 * ---- M1-2b: a userland client submits the batch ----
 *
 * The client writes only into the shared batch, which it reaches through
 * the command mmap window, and then asks the kernel to run it.  It never
 * names a register and never sees MMIO.  What the kernel decides, and the
 * client cannot influence, is the clip and the pitch -- so the walls a
 * batch is drawn inside are not part of what the batch can say.
 *
 * Diagnostics come back through a separate read parameter rather than the
 * IOReturn, because a client that is refused should be able to say which
 * field was wrong; a bare failure code would send the next person to the
 * kernel log for something they could have been told.
 */
#define OSMGA_HW3D_SUBMIT_PARAM "OSMGAHW3DSubmit"
#define OSMGA_HW3D_STATUS_PARAM "OSMGAHW3DStatus"
#define OSMGA_HW3D_CLIP_ROWS    120UL
#define OSMGA_HW3D_CLIP_COLS    64UL

static unsigned osmgaHW3DLast[4];   /* verdict, bad triangle, dwords, spins */

/*
 * The batch the kernel actually trusts.
 *
 * The shared batch stays mapped writable in the client, and the kernel
 * reads it twice -- once to validate, once to encode.  Between those two
 * reads another thread can rewrite it, and then the values that were
 * checked are not the values that reach the card.  The busy flag does not
 * help: it serialises submissions against each other, not a writer against
 * a submission.
 *
 * So the submit path copies first and works only from the copy.  Nothing
 * downstream of the copy can be changed by anyone but us.
 */
static OSMGAHW3DBatch osmgaHW3DSnapshot;

static unsigned long osmgaHW3DEncode(unsigned long *list,
                                     unsigned long listDwords,
                                     const OSMGAHW3DBatch *b,
                                     unsigned long *outTail);

#define MGA_DMA_GENERAL         0x00     /* PRIMADDRESS mode bits */
#define MGA_PRIMNOSTART         0x01     /* PRIMEND bit0: do NOT start */
#define MGA_PAGPXFER            0x02     /* PRIMEND bit1: AGP; 0 for PCI */

/*
 * A DMA block is one packed index dword followed by four value dwords, so
 * it always writes exactly four registers; DMAPAD fills unused slots.
 *
 * Only two register windows are reachable this way.  The DRM's own macro
 * does not check -- anything outside group 0 silently gets encoded with the
 * group 1 formula (mga_drv.h:234) -- so ours rejects instead.  That check
 * is a containment mechanism, not tidiness: a malformed list is executed by
 * the card, and DWGCTL bit 31 (CLIPDIS) would switch off the very clipping
 * the offscreen bound relies on.
 */
#define OSMGA_DMA_BLOCK_DWORDS  5
#define OSMGA_DMA_BLOCK_BYTES   (OSMGA_DMA_BLOCK_DWORDS * 4UL)
#define OSMGA_DWGREG0           0x1c00
#define OSMGA_DWGREG0_END       0x1dff
#define OSMGA_DWGREG1           0x2c00
#define OSMGA_DWGREG1_END       0x2dff
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
    static int warned = 0;
    unsigned long spins;

    /* FIFOSTATUS cannot report more free slots than the FIFO holds, so a
     * request above that never succeeds -- it just spins out and, in a
     * probe, exits silently.  The DDX clamps for exactly this reason
     * (mga_macros.h:34), and writing past the free count is safe: the
     * card stalls the bus rather than dropping the write. */
    if (slots > OSMGA_S1_FIFO_MAX) {
        if (!warned) {
            warned = 1;
            IOLog("OpenStepMGA: fifo request %u clamped to %u\n",
                  slots, (unsigned int)OSMGA_S1_FIFO_MAX);
        }
        slots = OSMGA_S1_FIFO_MAX;
    }
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

/*
 * ---- S4a character device: offscreen VRAM window ----
 *
 * These three values are written ONCE, before the cdevsw entry is published,
 * and are never modified afterwards.  The handler's decision depends on
 * nothing else (see the determinism requirement above).
 */
/*
 * M1-0 -- a second mmap window, for the command ring in system RAM.
 *
 * The existing window maps VRAM, and the command buffer a userland Mesa
 * would fill lives in system RAM, so one handler has to serve two
 * physical bases.  Partitioning the offset space does that without
 * touching the S4a contract: the handler stays a pure function of state
 * that is immutable once the cdevsw entry is published, which matters
 * because the kernel calls it twice for the same offset and ignores the
 * second call's return -- a handler that changed its mind between the
 * two would hand the kernel an arbitrary page frame.
 *
 * The command base is far above any VRAM offset (VRAM is at most 16 MiB)
 * and well inside the positive range of the int the handler is given.
 *
 * The ring is allocated once here and never freed.  IOMallocLow draws on
 * conventional memory, which is scarce, so this is gated behind the same
 * development switch as the VRAM window rather than always held.
 */
#define OSMGA_CMD_MMAP_BASE     0x40000000UL

static unsigned long osmgaMmapCmdPhysical;   /* 0 = no command window */
static unsigned long osmgaMmapCmdBytes;
static void *osmgaMmapCmdVirt;

static unsigned long osmgaMmapWindowStart;   /* byte offset into VRAM */
static unsigned long osmgaMmapWindowEnd;     /* exclusive */
static unsigned long osmgaMmapFbPhysical;
static int osmgaMmapRegistered;              /* class-level, register once */
static int osmgaMesaAccelEnabled;             /* M1-3a: Configure.app switch */
/*
 * M1-3a: the ioctl handler is a plain C function with no instance, and the
 * state it must report lives in instance variables.  Rather than mirror that
 * state into file scope, where the copy could fall behind, keep the instance
 * and ask it.  Set where the cdevsw entry is published, so it is never
 * non-nil for a device that cannot be opened; the driver already must not be
 * unloaded once that device exists (S4a), which is what makes this safe.
 */
static id osmgaCapsInstance;

/* One uncached word of the window, kept so a submission can settle a read
 * without mapping anything.  See where it is used. */
static volatile unsigned long *osmgaSettleAlias;

/*
 * REMAINING_WORK 3-10.  The submit path claims stormBusy, but mode changes
 * never did, so nothing stopped a mode from being reprogrammed while a batch
 * was setting up DMA -- the two would have been writing engine registers at
 * the same time.  Mode changes now join the same protocol.
 *
 * A mode change may not simply fail; the window server is asking for it.  So
 * it waits, bounded: five times the longest a submit can spin, which is a
 * margin rather than a guess.  If it still cannot claim the engine, it says
 * so loudly and goes ahead, because a display that never comes back is worse
 * than a batch drawn over -- and the epoch below is what keeps that case from
 * being reported as a success.
 */
#define OSMGA_MODE_CLAIM_SPINS  200UL
#define OSMGA_MODE_CLAIM_DELAY  5000     /* microseconds; 200 x 5ms = 1s */

static unsigned long osmgaModeEpoch;         /* bumped when a mode change ends */
static unsigned long statModeWaitedForEngine;
static unsigned long statModeProceededBusy;

/* Unused switch slots: refuse rather than leaving a NULL pointer behind. */
static int
osmgaDevNotSupported(void)
{
    return ENODEV;
}

static int
osmgaDevOpen(int dev, int flag, int devtype)
{
    (void)flag;
    (void)devtype;
    /* open() is an ordinary syscall context; a log here is safe and tells us
     * whether our cdevsw slot is actually the one being reached. */
    IOLog("OpenStepMGA S4a: open dev=%04x minor=%d registered=%d\n",
          dev & 0xFFFF, OSMGA_DEV_MINOR(dev), osmgaMmapRegistered);
    if (OSMGA_DEV_MINOR(dev) != 0)
        return ENXIO;
    if (!osmgaMmapRegistered)
        return ENXIO;
    return 0;
}

static int
osmgaDevClose(int dev, int flag, int devtype)
{
    (void)dev;
    (void)flag;
    (void)devtype;
    return 0;
}

/*
 * M1-3a: report the 3D capabilities to a caller that cannot use Objective-C.
 *
 * The kernel has already copied the caller's block in and will copy our
 * answer back out, sized from the encoded command, so this only fills it.
 */
static int
osmgaDevIoctl(int dev, int cmd, caddr_t data, int flag)
{
    OSMGAHW3DCapsBlock *blk = (OSMGAHW3DCapsBlock *)data;
    unsigned words[OSMGA_HW3D_CAPS_COUNT];
    unsigned i;

    (void)flag;
    if (OSMGA_DEV_MINOR(dev) != 0)
        return ENXIO;
    if (blk == 0 || osmgaCapsInstance == nil)
        return ENXIO;

    if ((unsigned long)cmd == OSMGA_IOC_CAPS) {
        [osmgaCapsInstance osmgaFillHW3DCaps:words];
        for (i = 0U; i < OSMGA_HW3D_CAPS_COUNT; i++)
            blk->caps[i] = (unsigned long)words[i];
        return 0;
    }

    if ((unsigned long)cmd == OSMGA_IOC_SUBMIT) {
        OSMGAHW3DSubmitBlock *sub = (OSMGAHW3DSubmitBlock *)data;
        IOReturn rc = [osmgaCapsInstance runHW3DSubmit];

        /*
         * The four words come from the same place the status parameter
         * reads, so a caller that submits and then asks gets one answer, not
         * two -- and it gets it without a second call another client's
         * submission could land in the middle of.
         */
        sub->verdict  = (unsigned long)osmgaHW3DLast[0];
        sub->triangle = (unsigned long)osmgaHW3DLast[1];
        sub->dwords   = (unsigned long)osmgaHW3DLast[2];
        sub->spins    = (unsigned long)osmgaHW3DLast[3];

        /*
         * Zero here, whatever happened to the batch: a 4.3BSD ioctl copies
         * the block back only when the handler returns zero, so refusing
         * would discard the explanation the caller came for.  The outcome is
         * in the block.
         */
        if (rc == IO_R_SUCCESS)            sub->status = 0UL;
        else if (rc == IO_R_UNSUPPORTED)   sub->status = ENODEV;
        else if (rc == IO_R_INVALID_ARG)   sub->status = EINVAL;
        else if (rc == IO_R_BUSY)          sub->status = EBUSY;
        else                               sub->status = EIO;
        return 0;
    }

    return ENOTTY;
}

/*
 * Pure arithmetic over immutable state.  No allocation, no sleeping, no I/O,
 * no IOLog, no locks: this runs inside VM-object construction, may hold VM
 * locks, and is called repeatedly for the same offset.
 */
static int
osmgaDevMmap(int dev, int offset, int prot)
{
    unsigned long off, phys;

    if (OSMGA_DEV_MINOR(dev) != 0 || offset < 0)
        return -1;
    if (prot != OSMGA_PROT_RW)                     /* no EXEC, no read-only */
        return -1;

    off = (unsigned long)offset;

    /* Command window first: its base is far above every VRAM offset, so
     * the two ranges cannot overlap however the VRAM window is sized. */
    if (osmgaMmapCmdPhysical != 0UL && off >= OSMGA_CMD_MMAP_BASE) {
        unsigned long rel = off - OSMGA_CMD_MMAP_BASE;

        /*
         * Only the batch, never the ring.  The kernel encodes its command
         * list into the second part of this same allocation, and a client
         * that could map it could overwrite the list after it had been
         * validated and before the engine read it -- which would let raw,
         * unchecked commands reach the engine and make the whole
         * validate-then-encode split pointless.  The client writes a batch;
         * the list is ours.
         */
        if (rel >= OSMGA_HW3D_BATCH_BYTES)
            return -1;
        if (osmgaMmapCmdBytes < (unsigned long)PAGE_SIZE ||
            rel > osmgaMmapCmdBytes - (unsigned long)PAGE_SIZE)
            return -1;
        if (osmgaMmapCmdPhysical > 0xFFFFFFFFUL - rel)
            return -1;
        phys = osmgaMmapCmdPhysical + rel;
        if ((phys & ((unsigned long)PAGE_SIZE - 1UL)) != 0UL)
            return -1;
        if ((phys >> PAGE_SHIFT) > 0x7FFFFFFFUL)
            return -1;
        return (int)(phys >> PAGE_SHIFT);
    }

    /* Written so no expression can overflow. */
    if (osmgaMmapWindowStart >= osmgaMmapWindowEnd ||
        osmgaMmapWindowEnd - osmgaMmapWindowStart < (unsigned long)PAGE_SIZE ||
        off < osmgaMmapWindowStart ||
        off > osmgaMmapWindowEnd - (unsigned long)PAGE_SIZE)
        return -1;

    if (osmgaMmapFbPhysical > 0xFFFFFFFFUL - off)
        return -1;
    phys = osmgaMmapFbPhysical + off;
    if ((phys & ((unsigned long)PAGE_SIZE - 1UL)) != 0UL)
        return -1;
    if ((phys >> PAGE_SHIFT) > 0x7FFFFFFFUL)
        return -1;
    return (int)(phys >> PAGE_SHIFT);
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
    dmaRingTestEnabled = NO;
    warpTestEnabled = NO;
    rasterTestEnabled = NO;
    stormBlitReady = NO;
    stormBlitFailed = NO;
    stormBusy = NO;
    simple_lock_init(&stormLock);
    statBlitRequests = 0; statBlitOk = 0; statBlitNoop = 0;
    statRefusedDisabled = 0; statRefusedGeometry = 0; statRefusedBusy = 0;
    statRefusedPreExec = 0; statPostExecTimeout = 0;
    statCursorShow = 0; statCursorMove = 0; statCursorHide = 0;
    statCursorWhileBusy = 0; statThin1px = 0;
    statEnterLinear = 0; statRevertVGA = 0; statTransferTable = 0;
    configuredVideoMemoryBytes = 0;

    if (!osmgaFindMGAFunction(&bus, &dev, &fn, &vendorDevice, &revision)) {
        IOLog("OpenStepMGAReplacementDisplay: MGA absent after probe, abort\n");
        return [super free];
    }
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

    }

    /* Opt-in D1 primary-DMA ring test; absent/anything-but-Yes means off. */
    {
        IOConfigTable *ct = [deviceDescription configTable];
        const char *flag = (ct == nil) ? 0
                         : (const char *)[ct valueForStringKey:"DMA Ring Test"];
        dmaRingTestEnabled = (flag != 0 && osmgaTextContains(flag, "Yes"))
                             ? YES : NO;
        if (dmaRingTestEnabled)
            IOLog("OpenStepMGAReplacementDisplay: D1 DMA ring test ENABLED "
                  "by configuration\n");
    }

    /* Opt-in D2 WARP configuration probe; absent/anything-but-Yes is off. */
    {
        IOConfigTable *ct = [deviceDescription configTable];
        const char *flag = (ct == nil) ? 0
                         : (const char *)[ct valueForStringKey:"WARP Test"];
        warpTestEnabled = (flag != 0 && osmgaTextContains(flag, "Yes"))
                          ? YES : NO;
        if (warpTestEnabled)
            IOLog("OpenStepMGAReplacementDisplay: D2 WARP test ENABLED "
                  "by configuration\n");
    }

    /* Opt-in D3 rasteriser probe; absent/anything-but-Yes is off. */
    {
        IOConfigTable *ct = [deviceDescription configTable];
        const char *flag = (ct == nil) ? 0
                         : (const char *)[ct valueForStringKey:"Raster Test"];
        rasterTestEnabled = (flag != 0 && osmgaTextContains(flag, "Yes"))
                            ? YES : NO;
        if (rasterTestEnabled)
            IOLog("OpenStepMGAReplacementDisplay: D3 raster test ENABLED "
                  "by configuration\n");
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
         * Capability flags must be set where IODisplayInfo is published --
         * once the window server has read them, setting them later is too
         * late.  We advertise the hardware CLUT because we do have one and do
         * implement setTransferTable:count:.
         *
         * IO_DISPLAY_CAN_BLIT is deliberately NOT advertised: measurement
         * showed this OPENSTEP's window server never sends IODisplayDoBlit --
         * the string does not appear in WindowServer or mach_kernel at all
         * (docs/S3B_PREP_INSTRUMENTATION_PLAN.md 11).
         */
        displayInfo->flags |= IO_DISPLAY_HAS_TRANSFER_TABLE;
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

    /*
     * S4a: publish the offscreen VRAM window as a character device, if the
     * configuration asks for it.  Gating is done HERE, by not registering at
     * all -- never by a flag the mmap handler consults, because that handler
     * must give the same answer every time it is asked (see its comment).
     */
    {
        IOConfigTable *ct = [deviceDescription configTable];
        const char *accel = (ct == nil) ? 0
                          : (const char *)[ct valueForStringKey:
                                              "Mesa Acceleration"];

        /* M1-3a: read it here but gate nothing here.  Unlike "VRAM Mmap",
         * which decides whether a device is published at all, this switch is
         * only reported through the capability parameter -- the library is
         * what declines to accelerate.  Keeping the driver's behaviour
         * identical either way means the switch cannot break the display. */
        osmgaMesaAccelEnabled =
            (accel != 0 && osmgaTextContains(accel, "Yes")) ? 1 : 0;
        IOLog("OpenStepMGA M1-3a: Mesa acceleration switch is %s\n",
              osmgaMesaAccelEnabled ? "Yes" : "No");
    }
    {
        IOConfigTable *ct = [deviceDescription configTable];
        const char *flag = (ct == nil) ? 0
                         : (const char *)[ct valueForStringKey:"VRAM Mmap"];
        if (flag != 0 && osmgaTextContains(flag, "Yes") && !osmgaMmapRegistered) {
            const OSMGARes *wr = &osmgaRes[selectedResIndex];
            const OSMGAFormat *wf = &osmgaFmt[selectedFormatIndex];
            unsigned long visEnd =
                (unsigned long)wr->width * (unsigned long)wf->bytesPerPixel *
                (unsigned long)wr->height;
            unsigned long guard =
                OSMGA_MMAP_GUARD_ROWS * (unsigned long)wr->width *
                (unsigned long)wf->bytesPerPixel;
            unsigned long start =
                (visEnd + guard + (unsigned long)PAGE_SIZE - 1UL) &
                ~((unsigned long)PAGE_SIZE - 1UL);
            unsigned long end =
                OSMGA_S1_VRAM_PROVEN & ~((unsigned long)PAGE_SIZE - 1UL);

            if (start >= end ||
                end - start < (unsigned long)PAGE_SIZE ||
                end > MGA_VRAM_16MB) {
                IOLog("OpenStepMGA S4a: no usable offscreen window for this "
                      "mode (start=%lu end=%lu), device NOT registered\n",
                      start, end);
            } else {
                /* Immutable state first, cdevsw entry published after.
                 * The ring is optional: without it the VRAM window still
                 * works, and the handler simply refuses the command
                 * range because its base stays zero. */
                void *ring = 0;
                unsigned int ringPhysRaw = 0U;
                unsigned long ringPhys = 0UL;

                /* The command branch is tried first, so a VRAM window that
                 * ever reached the command base would be swallowed by it.
                 * Today it cannot -- VRAM is at most 16 MiB and the base is
                 * at 1 GiB -- but that is a property of the numbers, not of
                 * the code, so make it a checked invariant instead. */
                if (end > OSMGA_CMD_MMAP_BASE) {
                    IOLog("OpenStepMGA M1-0: VRAM window reaches the command "
                          "base, command window NOT offered\n");
                } else {
                    ring = IOMallocLow((int)OSMGA_DMA_RING_BYTES);
                }
                if (ring != 0) {
                    IOReturn rr = IOPhysicalFromVirtual(IOVmTaskSelf(),
                                      (vm_address_t)ring, &ringPhysRaw);

                    ringPhys = (unsigned long)ringPhysRaw;
                    if (rr != IO_R_SUCCESS ||
                        (ringPhys & ((unsigned long)PAGE_SIZE - 1UL)) != 0UL) {
                        IOLog("OpenStepMGA M1-0: ring physical address "
                              "unusable (r=%d phys=%08lx), command window "
                              "not offered\n", (int)rr, ringPhys);
                        IOFreeLow(ring, (int)OSMGA_DMA_RING_BYTES);
                        ring = 0;
                        ringPhys = 0UL;
                    }
                } else {
                    IOLog("OpenStepMGA M1-0: IOMallocLow failed, command "
                          "window not offered\n");
                }
                osmgaMmapCmdVirt     = ring;
                osmgaMmapCmdPhysical = ringPhys;
                osmgaMmapCmdBytes    = (ring != 0) ? OSMGA_DMA_RING_BYTES : 0UL;

                osmgaMmapWindowStart = start;
                osmgaMmapWindowEnd   = end;
                osmgaMmapFbPhysical  = frameBufferPhysical;
                if ([[self class]
                        addToCdevswFromDescription:deviceDescription
                          open:(IOSwitchFunc)osmgaDevOpen
                         close:(IOSwitchFunc)osmgaDevClose
                          read:(IOSwitchFunc)osmgaDevNotSupported
                         write:(IOSwitchFunc)osmgaDevNotSupported
                         ioctl:(IOSwitchFunc)osmgaDevIoctl
                          stop:(IOSwitchFunc)osmgaDevNotSupported
                         reset:(IOSwitchFunc)osmgaDevNotSupported
                        select:(IOSwitchFunc)osmgaDevNotSupported
                          mmap:(IOSwitchFunc)osmgaDevMmap
                          getc:(IOSwitchFunc)osmgaDevNotSupported
                          putc:(IOSwitchFunc)osmgaDevNotSupported]) {
                    /* Instance first, then the gate.  open() refuses
                     * while osmgaMmapRegistered is zero, so setting it last
                     * means nobody can be inside ioctl before the receiver
                     * exists.  The other order leaves a window in which an
                     * open succeeds and the probe immediately fails. */
                    /*
                     * One page of the window, aliased uncached and kept, so
                     * that every submission can read video memory without
                     * mapping anything.  Only used to settle a read; if it
                     * cannot be made, submissions still work and the client
                     * is the one that notices.
                     */
                    {
                        vm_address_t sa = 0;
                        unsigned long sl = 0UL;
                        volatile unsigned long *sp = 0;

                        if (osmgaMapUncachedBlock(frameBufferPhysical, start,
                                                  start + (unsigned long)PAGE_SIZE,
                                                  &sa, &sl, &sp) == IO_R_SUCCESS)
                            osmgaSettleAlias = sp;
                        else
                            IOLog("OpenStepMGA 3-18: no uncached alias; a "
                                  "client's first read after a submission may "
                                  "see what was there before\n");
                    }
                    osmgaCapsInstance = self;
                    osmgaMmapRegistered = 1;
                    IOLog("OpenStepMGA S4a: PAGE_SIZE=%lu PAGE_SHIFT=%lu "
                          "fbPhys=%08lx firstPFN=%lx\n",
                          (unsigned long)PAGE_SIZE, (unsigned long)PAGE_SHIFT,
                          osmgaMmapFbPhysical,
                          (osmgaMmapFbPhysical + start) >> PAGE_SHIFT);
                    IOLog("OpenStepMGA S4a: VRAM window %lu..%lu (%lu KiB) "
                          "as character major %d; the driver must NOT be "
                          "unloaded while this is enabled (mappings outlive "
                          "it)\n", start, end - 1UL, (end - start) / 1024UL,
                          [[self class] characterMajor]);
                } else {
                    osmgaMmapWindowStart = 0;
                    osmgaMmapWindowEnd = 0;
                    osmgaMmapFbPhysical = 0;
                    IOLog("OpenStepMGA S4a: cdevsw registration failed; "
                          "continuing without the VRAM device\n");
                }
            }
        }
    }

    IOLog("OpenStepMGAReplacementDisplay: init ok fb=%08x mmio=%08x %s %s\n",
          (unsigned int)displayInfo->frameBuffer, (unsigned int)mmioBase,
          osmgaRes[selectedResIndex].name, osmgaFmt[selectedFormatIndex].cspace);
    return self;
}

- (BOOL)readManualMemoryConfiguration:configTable
{
    OSMGAManualMemoryStatus status;

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

- (void)osmgaFillHW3DCaps:(unsigned *)capsOut
{
    unsigned long flags = 0UL;

    /*
     * MMAP, CMD and READY are the same predicate the submit path tests before
     * it returns IO_R_RESOURCE, so the two cannot disagree about what the
     * state means.
     *
     * They are NOT atomic with respect to it.  revertToVGAMode clears
     * linearModeActive under no lock, so a caller can read READY here, lose
     * the mode, and be refused by submit -- and that is the intended outcome,
     * not a contradiction.  What the matching predicate buys is narrower than
     * it looks: a refusal after a positive answer means the state changed in
     * between, never that the two disagree.  (The larger hazard, submit
     * passing its own gate and then racing a mode change while it programs
     * DMA, is older than this parameter; see REMAINING_WORK.)
     */
    if (osmgaMmapRegistered)
        flags |= OSMGA_HW3D_CAP_MMAP;
    if (osmgaMmapCmdVirt != 0 && osmgaMmapCmdPhysical != 0UL)
        flags |= OSMGA_HW3D_CAP_CMD;
    if (mmioMapped && linearModeActive &&
        osmgaFmt[selectedFormatIndex].bytesPerPixel == 4)
        flags |= OSMGA_HW3D_CAP_READY;
    if (osmgaMesaAccelEnabled)
        flags |= OSMGA_HW3D_CAP_ENABLED;

    capsOut[OSMGA_HW3D_CAP_MAGIC]   = (unsigned)OSMGA_HW3D_MAGIC;
    capsOut[OSMGA_HW3D_CAP_VERSION] = (unsigned)OSMGA_HW3D_VERSION;
    capsOut[OSMGA_HW3D_CAP_FLAGS]   = (unsigned)flags;
    capsOut[OSMGA_HW3D_CAP_MAXTRI]  = (unsigned)OSMGA_HW3D_MAX_TRI;
    capsOut[OSMGA_HW3D_CAP_BATCH]   = (unsigned)OSMGA_HW3D_BATCH_BYTES;
    capsOut[OSMGA_HW3D_CAP_MAJOR]   = (unsigned)[[self class] characterMajor];
    capsOut[OSMGA_HW3D_CAP_VRAMOFF] = (unsigned)osmgaMmapWindowStart;
    /*
     * The stride the engine will use, not the one a caller might prefer: the
     * destination pitch comes from a single register holding the display's,
     * so a surface laid out any other way would be read wrongly however it
     * was written.
     */
    capsOut[OSMGA_HW3D_CAP_STRIDE]  =
        (unsigned)((unsigned long)[self displayInfo]->rowBytes / 4UL);
    capsOut[OSMGA_HW3D_CAP_VRAMLEN] =
        (unsigned)(osmgaMmapWindowEnd - osmgaMmapWindowStart);
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
        parameterArray[18] = statTransferTable;
        *count = OSMGA_STATS_COUNT;
        return IO_R_SUCCESS;
    }
    if (osmgaTextEquals(parameterName, OSMGA_HW3D_CAPS_PARAM)) {
        if (parameterArray == 0 || count == 0 ||
            *count != OSMGA_HW3D_CAPS_COUNT)
            return IO_R_INVALID_ARG;
        [self osmgaFillHW3DCaps:parameterArray];
        *count = OSMGA_HW3D_CAPS_COUNT;
        return IO_R_SUCCESS;
    }
    if (osmgaTextEquals(parameterName, OSMGA_HW3D_STATUS_PARAM)) {
        unsigned i;

        /* count is a POINTER here, unlike in setIntValues; the exact-size
         * contract is the one the stats parameter above already uses. */
        if (parameterArray == 0 || count == 0 || *count != 4U)
            return IO_R_INVALID_ARG;
        for (i = 0U; i < 4U; i++)
            parameterArray[i] = osmgaHW3DLast[i];
        *count = 4U;
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
    /*
     * S4a: solid-fill a rectangle that lies wholly inside the mmap window, so
     * a userspace client can prove the engine and its mapping see the same
     * memory (and, crucially, run the stale-cache test).  Refused unless the
     * window is registered, and validated against the WINDOW, not the screen.
     * Parameters: [x, y, w, h, colour].
     */
    if (osmgaTextEquals(parameterName, OSMGA_PROBE_FILL_PARAM)) {
        const OSMGARes *r2 = &osmgaRes[selectedResIndex];
        const OSMGAFormat *f2 = &osmgaFmt[selectedFormatIndex];
        unsigned long stride, first, last;
        unsigned x, y, w2, h2, colour;
        int rc;

        if (!osmgaMmapRegistered || stormBlitFailed || !stormBlitReady)
            return IO_R_RESOURCE;
        if (!mmioMapped || !linearModeActive || f2->bytesPerPixel != 4)
            return IO_R_RESOURCE;
        if (parameterArray == 0 || count != 5)
            return IO_R_RESOURCE;
        x = parameterArray[0]; y = parameterArray[1];
        w2 = parameterArray[2]; h2 = parameterArray[3];
        colour = parameterArray[4];
        if (((x | y | w2 | h2) & 0x80000000U) || w2 == 0U || h2 == 0U)
            return IO_R_RESOURCE;
        stride = (unsigned long)r2->width;
        if ((unsigned long)x + w2 > stride)
            return IO_R_RESOURCE;
        /* Whole rectangle must sit inside the registered window. */
        first = ((unsigned long)y * stride + x) * 4UL;
        last  = (((unsigned long)(y + h2 - 1U)) * stride + x + w2 - 1U) * 4UL
                + 4UL;
        if (first < osmgaMmapWindowStart || last > osmgaMmapWindowEnd)
            return IO_R_RESOURCE;

        simple_lock(&stormLock);
        if (stormBusy) { simple_unlock(&stormLock); return IO_R_RESOURCE; }
        stormBusy = YES;
        simple_unlock(&stormLock);

        rc = 0;
        if (osmgaStormWaitIdle(mmioBase) &&
            osmgaStormWaitFifo(mmioBase, 13U)) {
            osmgaStormInitState(mmioBase, stride, x, x + w2 - 1U,
                                (unsigned long)y * stride,
                                (unsigned long)(y + h2 - 1U) * stride);
            osmgaW32(mmioBase, MGA_DWGCTL, MGA_DWGCTL_SOLID_FILL);
            if (osmgaStormWaitFifo(mmioBase, 3U)) {
                osmgaW32(mmioBase, MGA_FCOL, (unsigned long)colour);
                osmgaW32(mmioBase, MGA_FXBNDRY,
                         (((unsigned long)x + w2) << 16) |
                         ((unsigned long)x & 0xffffUL));
                osmgaW32(mmioBase, MGA_YDSTLEN + MGA_EXEC,
                         ((unsigned long)y << 16) | h2);
                rc = osmgaStormWaitIdle(mmioBase) ? 1 : -1;
            }
        }
        if (rc < 0) {
            stormBlitFailed = YES;
            IOLog("OpenStepMGA S4a: fill execute timed out; acceleration "
                  "DISABLED permanently\n");
        }
        simple_lock(&stormLock);
        stormBusy = NO;
        simple_unlock(&stormLock);
        return (rc == 1) ? IO_R_SUCCESS : IO_R_RESOURCE;
    }

    if (osmgaTextEquals(parameterName, OSMGA_HW3D_SUBMIT_PARAM)) {
        (void)parameterArray;
        (void)count;
        return [self runHW3DSubmit];
    }

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

/*
 * M1-3b-3: one implementation of a submission, reached two ways.
 *
 * The parameter form needs IODeviceMaster, which is Objective-C, and libGL
 * cannot link that -- so the library reaches this through an ioctl on the
 * character device instead.  Both call here, so the two cannot drift, and
 * neither can quietly acquire a check the other lacks.
 */
- (IOReturn)runHW3DSubmit
{
    OSMGAHW3DBatch *batch;
    OSMGAHW3DLimits lim;
    IODisplayInfo *di3;   /* read under the claim, never before it */
    const OSMGAFormat *f3 = &osmgaFmt[selectedFormatIndex];
    unsigned long stride3, total3, tail3, listPhys3, spins3, status3;
    unsigned long epoch3, dstW3, dstH3, dstP3, avail3;
    unsigned long *list3, listDwords3, badTri3 = 0UL;
    int v3, rc3 = 0;

    /*
     * Clear the diagnostics first.  Several of the checks below return
     * before they are written, and the ioctl copies them back regardless, so
     * leaving them alone would answer this submission with the last one's
     * verdict -- worst of all reporting OSMGA_HW3D_OK for a batch that was
     * never looked at.
     */
    osmgaHW3DLast[0] = (unsigned)OSMGA_HW3D_NOT_RUN;
    osmgaHW3DLast[1] = 0U;
    osmgaHW3DLast[2] = 0U;
    osmgaHW3DLast[3] = 0U;

    /*
     * The switch is enforced here and not only reported.  Reporting alone
     * left an honest gap: "No" meant a cooperating library declined, but
     * anything written against this parameter could submit regardless, so
     * the switch did not mean what a person setting it would think.  A
     * library built against a different contract, or left behind by a
     * partial upgrade, would have gone straight past a cleared bit.
     *
     * Refusing costs nothing the display depends on: it is the same
     * refusal that already happens whenever the 3D path is not usable.
     */
    if (!osmgaMesaAccelEnabled)
        return IO_R_UNSUPPORTED;
    if (!osmgaMmapRegistered || !mmioMapped || !linearModeActive)
        return IO_R_RESOURCE;
    if (f3->bytesPerPixel != 4)
        return IO_R_RESOURCE;
    if (osmgaMmapCmdVirt == 0 || osmgaMmapCmdPhysical == 0UL)
        return IO_R_RESOURCE;

    /*
     * Claim the engine BEFORE copying the client's batch, not after
     * validating it.
     *
     * The snapshot is one global.  Taking it outside the claim meant a second
     * caller could overwrite it between the moment this one validated it and
     * the moment it was encoded -- so the batch that was proved and the batch
     * that was drawn were not the same batch, which is the whole thing the
     * snapshot was introduced to prevent.  Holding the engine across the copy,
     * the proof, the encoding and the drawing makes them one object again.
     *
     * It also settles the stride: everything below reads it once, under the
     * claim, so nothing derived from it can be left over from a mode that has
     * since changed.
     */
    simple_lock(&stormLock);
    if (stormBusy) { simple_unlock(&stormLock); return IO_R_BUSY; }
    stormBusy = YES;
    epoch3 = osmgaModeEpoch;
    simple_unlock(&stormLock);

    /*
     * Everything about the display is read again HERE, now that nothing else
     * can be programming it.  The values taken when this method was entered
     * belong to whatever mode was current then, and a mode change could have
     * finished in the meantime; the stride in particular reaches both the
     * proof below and the engine above, and proving a rectangle against one
     * pitch while the card draws it with another proves nothing.
     */
    di3 = [self displayInfo];
    if (!mmioMapped || !linearModeActive ||
        osmgaFmt[selectedFormatIndex].bytesPerPixel != 4) {
        simple_lock(&stormLock);
        stormBusy = NO;
        simple_unlock(&stormLock);
        return IO_R_RESOURCE;
    }
    stride3 = (unsigned long)di3->rowBytes / 4UL;
    batch = (OSMGAHW3DBatch *)osmgaMmapCmdVirt;
    list3 = (unsigned long *)((char *)osmgaMmapCmdVirt +
                              OSMGA_HW3D_RING_OFFSET);
    listDwords3 = (OSMGA_DMA_RING_BYTES - OSMGA_HW3D_RING_OFFSET) / 4UL;
    listPhys3 = osmgaMmapCmdPhysical + OSMGA_HW3D_RING_OFFSET;

    /* Every one of these comes from the kernel.  Nothing a client can
     * write reaches this structure. */
    /* pitchBytes comes from the batch and is set once it has been proved. */
    /* clipX1 and clipY1 come from the batch and are set below, once the
     * snapshot exists and the rectangle it declares has been proved to lie
     * inside the window. */
    /*
     * All three live anywhere in the window, and the window is the whole of
     * the safety property: a client that overlaps its own colour with its own
     * depth draws nonsense into memory it was given, which is its business,
     * while reaching outside is the thing none of this may allow.
     *
     * They used to be three fixed thirds, a convenience from when each probe
     * had a corner of its own -- and it stopped a real drawing surface from
     * putting its depth immediately after its colour, which is where a
     * surface wants it.
     */
    lim.colourStart = osmgaMmapWindowStart;
    lim.colourEnd   = osmgaMmapWindowEnd;
    lim.depthStart  = osmgaMmapWindowStart;
    lim.depthEnd    = osmgaMmapWindowEnd;
    lim.texStart    = osmgaMmapWindowStart;
    lim.texEnd      = osmgaMmapWindowEnd;
    lim.batchBytes  = OSMGA_HW3D_BATCH_BYTES;
    lim.maxEdgeWalk = OSMGA_HW3D_EDGE_WALK;

    /* Copy before checking.  triCount is read exactly once, because
     * reading it again after the copy would reintroduce the same
     * window it is here to close. */
    {
        unsigned long n3 = batch->triCount;
        unsigned long fixed3 = sizeof(OSMGAHW3DBatch) -
                   sizeof(OSMGAHW3DTri) * OSMGA_HW3D_MAX_TRI;
        unsigned long words3, i4;
        const unsigned long *src3 = (const unsigned long *)batch;
        unsigned long *dst3 = (unsigned long *)&osmgaHW3DSnapshot;

        if (n3 > OSMGA_HW3D_MAX_TRI) {
            osmgaHW3DLast[0] = (unsigned)OSMGA_HW3D_E_COUNT;
            osmgaHW3DLast[1] = 0U;
            osmgaHW3DLast[2] = 0U;
            osmgaHW3DLast[3] = 0U;
            simple_lock(&stormLock);
            stormBusy = NO;
            simple_unlock(&stormLock);
            return IO_R_INVALID_ARG;
        }
        words3 = (fixed3 + n3 * sizeof(OSMGAHW3DTri)) / 4UL;
        for (i4 = 0UL; i4 < words3; i4++)
            dst3[i4] = src3[i4];
        osmgaHW3DSnapshot.triCount = n3;
    }

    /*
     * The batch says how big its destination is; this is where that is
     * proved rather than believed.  Nothing here forms a product: a height
     * times a row of bytes leaves 32 bits long before it stops being a
     * plausible request, and a check that overflows is not a check.
     *
     * The width cannot exceed the stride, because the engine takes the
     * destination pitch from a single register holding the display's, and a
     * wider rectangle would simply wrap onto the row below.
     */
    dstW3 = osmgaHW3DSnapshot.state.dstWidth;
    dstH3 = osmgaHW3DSnapshot.state.dstHeight;
    dstP3 = osmgaHW3DSnapshot.state.dstPitch;

    /*
     * The pitch decides how far apart the rows are, for colour and for depth
     * alike, so it is what everything below is measured against.  It may not
     * exceed the display's: that width is already known to work in this
     * register, and allowing more would be asking the engine for something
     * nothing here has ever tried.  A row must fit inside it, or the row
     * would run into the next one.
     */
    if (dstP3 == 0UL || dstP3 > stride3 || dstW3 > dstP3) {
        osmgaHW3DLast[0] = (unsigned)OSMGA_HW3D_E_DSTPITCH;
        simple_lock(&stormLock);
        stormBusy = NO;
        simple_unlock(&stormLock);
        return IO_R_INVALID_ARG;
    }
    if (dstW3 == 0UL || dstH3 == 0UL) {
        osmgaHW3DLast[0] = (unsigned)OSMGA_HW3D_E_DSTSIZE;
        simple_lock(&stormLock);
        stormBusy = NO;
        simple_unlock(&stormLock);
        return IO_R_INVALID_ARG;
    }
    if (osmgaHW3DSnapshot.state.dstorg < osmgaMmapWindowStart ||
        osmgaHW3DSnapshot.state.dstorg >= osmgaMmapWindowEnd) {
        osmgaHW3DLast[0] = (unsigned)OSMGA_HW3D_E_DSTORG;
        simple_lock(&stormLock);
        stormBusy = NO;
        simple_unlock(&stormLock);
        return IO_R_INVALID_ARG;
    }
    /*
     * The last byte the engine can touch is dstorg + (h-1)*pitch*4 + w*4 - 1.
     * Compared without forming the product, because a height a caller may
     * legitimately ask for overflows a 32-bit multiply long before it stops
     * being plausible, and a check that overflows is not a check.
     */
    avail3 = osmgaMmapWindowEnd - osmgaHW3DSnapshot.state.dstorg;
    if (dstW3 * 4UL > avail3 ||
        dstH3 - 1UL > (avail3 - dstW3 * 4UL) / (dstP3 * 4UL)) {
        osmgaHW3DLast[0] = (unsigned)OSMGA_HW3D_E_DSTSIZE;
        simple_lock(&stormLock);
        stormBusy = NO;
        simple_unlock(&stormLock);
        return IO_R_INVALID_ARG;
    }
    lim.pitchBytes = dstP3 * 4UL;
    lim.clipX1 = dstW3 - 1UL;
    lim.clipY1 = dstH3 - 1UL;

    v3 = osmgaHW3DValidate(&osmgaHW3DSnapshot, &lim, &badTri3);
    osmgaHW3DLast[0] = (unsigned)v3;
    osmgaHW3DLast[1] = (unsigned)badTri3;
    osmgaHW3DLast[2] = 0U;
    osmgaHW3DLast[3] = 0U;
    if (v3 != OSMGA_HW3D_OK) {
        simple_lock(&stormLock);
        stormBusy = NO;
        simple_unlock(&stormLock);
        return IO_R_INVALID_ARG;
    }


    /*
     * No second look at the display here.  There used to be one, from when
     * the engine was claimed later than it is now: the geometry was read
     * before the claim and had to be re-read after it, and only the width
     * was re-proved, which left the pitch the proof and the validator had
     * used still belonging to the older mode.  With the claim taken before
     * anything is read, there is one reading and it cannot go stale.
     */
    total3 = osmgaHW3DEncode(list3, listDwords3, &osmgaHW3DSnapshot,
                             &tail3);
    osmgaHW3DLast[2] = (unsigned)total3;
    if (total3 != 0UL &&
        osmgaStormWaitIdle(mmioBase) &&
        osmgaStormWaitFifo(mmioBase, 13U)) {
        osmgaStormInitState(mmioBase, dstP3, 0UL, dstW3 - 1UL, 0UL,
                            (dstH3 - 1UL) * dstP3);
        osmgaW32(mmioBase, MGA_ICLEAR, MGA_SOFTRAPICLR);
        for (spins3 = 0UL; spins3 < OSMGA_S1_SPIN_LIMIT; spins3++) {
            status3 = osmgaR32(mmioBase, MGA_ENGSTATUS) &
                      MGA_DMA_DONE_MASK;
            if (status3 == MGA_STATUS_ENDPRDMASTS) break;
        }
        if (spins3 < OSMGA_S1_SPIN_LIMIT) {
            unsigned long sum3 = 0UL, i3;

            for (i3 = 0UL; i3 < total3; i3++) sum3 += list3[i3];
            (void)osmgaR32(mmioBase, MGA_ENGSTATUS);
            if (sum3 == 0xFFFFFFFFUL) IOLog("barrier %lu\n", sum3);

            osmgaW32(mmioBase, MGA_PRIMADDRESS,
                     listPhys3 | MGA_DMA_GENERAL);
            osmgaW32(mmioBase, MGA_PRIMEND,
                     (listPhys3 + tail3 * 4UL) | MGA_DMA_GENERAL);
            for (spins3 = 0UL; spins3 < OSMGA_S1_SPIN_LIMIT; spins3++) {
                status3 = osmgaR32(mmioBase, MGA_ENGSTATUS);
                if ((status3 & MGA_DMA_DONE_MASK) == MGA_DMA_DONE_VALUE)
                    break;
            }
            osmgaW32(mmioBase, MGA_ICLEAR, MGA_SOFTRAPICLR);
            osmgaHW3DLast[3] = (unsigned)spins3;
            if (spins3 < OSMGA_S1_SPIN_LIMIT &&
                osmgaStormWaitIdle(mmioBase)) {
                /*
                 * Read the destination back before saying it is done.
                 *
                 * Waiting for the DMA to reach its end and then for the
                 * engine to report idle does not settle what a client will
                 * see: measured, the first read a client made after this
                 * returned came back holding what was there before the draw,
                 * and the next read a moment later was correct.  It was not
                 * an address -- the word next door behaved the same way and
                 * then stopped -- and register reads do not help, since this
                 * path already makes many.  A read across the same memory
                 * did help, so the driver makes one and a client no longer
                 * has to know to.
                 *
                 * Through the UNCACHED alias, not through displayInfo's
                 * framebuffer pointer.  The comment on osmgaMapUncachedBlock
                 * says why, and it was written before this: that mapping's
                 * read cache attribute is unproven and volatile does not make
                 * a cached read coherent with an engine write.  Reading there
                 * could be answered from cache and settle nothing -- which is
                 * what the first version of this did.
                 */
                if (osmgaSettleAlias != 0)
                    (void)osmgaSettleAlias[0];
                rc3 = 1;
            }
            else
                IOLog("OpenStepMGA M1-2b: submission did not complete "
                      "(status %08lx, spins %lu)\n", status3, spins3);
        }
    }
    simple_lock(&stormLock);
    stormBusy = NO;
    /*
     * A mode change that could not claim the engine goes ahead regardless
     * (see -claimEngineForMode).  If one did that while we were
     * drawing, whatever we drew went somewhere we can no longer describe,
     * so it is reported as a failure rather than a success.
     */
    if (osmgaModeEpoch != epoch3)
        rc3 = 0;
    simple_unlock(&stormLock);
    if (rc3 == 0 && osmgaModeEpoch != epoch3)
        IOLog("OpenStepMGA 3-10: the mode changed while a batch was "
              "drawing; reporting it as failed\n");
    return (rc3 == 1) ? IO_R_SUCCESS : IO_R_RESOURCE;
}
- (int)claimEngineForMode
{
    unsigned long spins;

    for (spins = 0UL; spins < OSMGA_MODE_CLAIM_SPINS; spins++) {
        simple_lock(&stormLock);
        if (!stormBusy) {
            stormBusy = YES;
            simple_unlock(&stormLock);
            if (spins != 0UL)
                statModeWaitedForEngine++;
            return 1;
        }
        simple_unlock(&stormLock);
        IODelay(OSMGA_MODE_CLAIM_DELAY);
    }
    statModeProceededBusy++;
    IOLog("OpenStepMGA 3-10: the engine stayed busy for a second; changing "
          "mode anyway, and any batch in flight will be told it failed\n");
    return 0;
}

- (void)releaseEngineAfterMode:(int)claimed
{
    simple_lock(&stormLock);
    if (claimed)
        stormBusy = NO;
    osmgaModeEpoch++;
    simple_unlock(&stormLock);
}

- (void)enterLinearMode
{
    /* The claim covers the register programming and stops there.  It must
     * NOT be held across the tests below: they claim the engine themselves
     * and would wait on us for a second each before giving up. */
    int claimed = [self claimEngineForMode];

    statEnterLinear++;
    IOLog("OpenStepMGAReplacementDisplay: enterLinearMode begin\n");
    if (![self programLinearMode]) {
        [self releaseEngineAfterMode:claimed];
        IOLog("OpenStepMGAReplacementDisplay: enterLinearMode FAILED; recover via "
              "R5-VGA config-edit reboot (Active Drivers -> VGA)\n");
        return;
    }
    [self releaseEngineAfterMode:claimed];
    IOLog("OpenStepMGAReplacementDisplay: enterLinearMode done\n");
    /* Opt-in engine tests, last of all: display is already up and verified.
     * S2 runs after S1 and reuses the same offscreen area for its source. */
    [self runStormLivenessTest];
    [self runStormBlitTest];
    [self runStormBlitApiTest];
    [self runDmaRingAllocTest];
    [self runDmaRingBuildTest];
    [self runDmaRingStartTest];
    [self runWarpConfigTest];
    [self runWarpUcodePlacementTest];
    [self runWarpPipeStartTest];
    /* D3-0/1/2 and D3-3a-0 are recorded in docs/ and their output costs
     * about ten syslog lines.  syslog drops bursts, and it dropped the
     * whole of D3-3b's first run, so leave them out while D3-3b is the
     * measurement.  Restore both calls once depth is settled. */
    /* [self runRasterInterpolationTest]; */
    /* [self runDstorgOriginTest]; */
    [self runAlphaBlendTest];
    [self runMmapWindowTest];
    [self runHW3DBatchTest];
    [self runHW3DContainmentTest];
}

/*
 * D1-0 -- prove the ring memory before touching the card.
 *
 * Answers three things and stops: does IOMallocLow resolve and succeed in a
 * kernel loadable, where does the memory actually land, and is it really
 * physically contiguous.  The contiguity check walks the buffer page by page
 * with IOPhysicalFromVirtual instead of trusting the bump-allocator reading
 * of alloc_cnvmem -- the analysis says it must be contiguous, so measure it.
 *
 * PAGE_SIZE here is 8192, not 4096 (S4a); the walk uses the kernel's own
 * value rather than a literal for that reason.
 *
 * No engine register is written.  The buffer is released before returning:
 * the card is never told about it, so nothing can still be reading it.  That
 * is the opposite of D1-2, where release is gated on proven DMA quiescence.
 */

/*
 * D1-1 -- assemble a primary DMA command list.
 *
 * Returns the index byte for a register, or -1 if the register cannot be
 * written by DMA at all.  OPMODE (0x1e54) is the case that matters here:
 * the Storm setup S1 uses writes it, and it falls between the two windows,
 * so it has to stay an MMIO write made before the list runs.
 */
static int
osmgaDmaIndex(unsigned long reg)
{
    /* The secondary and setup rings take addresses the same way the primary
     * ring does, and unlike PRIMADDRESS and PRIMEND -- which sit at 0x1e58
     * and 0x1e5c, outside both groups, so the group rule alone excludes
     * them -- these four are inside group1 and the rule would encode them.
     * No caller names them today, but the encoder should not be the thing
     * standing between a future list-building caller and a register that
     * makes the card walk memory of its choosing. */
    if (reg == MGA_SECADDRESS || reg == MGA_SECEND ||
        reg == MGA_SETUPADDRESS || reg == MGA_SETUPEND)
        return -1;
    if (reg >= OSMGA_DWGREG0 && reg <= OSMGA_DWGREG0_END)
        return (int)((reg - OSMGA_DWGREG0) >> 2);
    if (reg >= OSMGA_DWGREG1 && reg <= OSMGA_DWGREG1_END)
        return (int)(((reg - OSMGA_DWGREG1) >> 2) | 0x80UL);
    return -1;
}

/*
 * Append one block.  Refuses rather than truncates or mis-encodes: a
 * rejected register leaves *pos untouched and returns 0, and every caller
 * checks, so a list is either complete and correct or never started.
 */
static int
osmgaDmaBlock(unsigned long *ring, unsigned long ringDwords, unsigned long *pos,
              unsigned long r0, unsigned long v0,
              unsigned long r1, unsigned long v1,
              unsigned long r2, unsigned long v2,
              unsigned long r3, unsigned long v3)
{
    int i0 = osmgaDmaIndex(r0);
    int i1 = osmgaDmaIndex(r1);
    int i2 = osmgaDmaIndex(r2);
    int i3 = osmgaDmaIndex(r3);
    unsigned long p = *pos;

    if (i0 < 0 || i1 < 0 || i2 < 0 || i3 < 0) {
        IOLog("OpenStepMGA D1-1: REJECT block, register not DMA-addressable "
              "(%04x %04x %04x %04x)\n",
              (unsigned)r0, (unsigned)r1, (unsigned)r2, (unsigned)r3);
        return 0;
    }
    if (p + OSMGA_DMA_BLOCK_DWORDS > ringDwords) {
        IOLog("OpenStepMGA D1-1: REJECT block, ring full at dword %u\n",
              (unsigned)p);
        return 0;
    }

    ring[p++] = ((unsigned long)i0)        | (((unsigned long)i1) << 8) |
                (((unsigned long)i2) << 16) | (((unsigned long)i3) << 24);
    ring[p++] = v0;
    ring[p++] = v1;
    ring[p++] = v2;
    ring[p++] = v3;
    *pos = p;
    return 1;
}

- (void)runDmaRingAllocTest
{
    void *virt;
    unsigned phys = 0;
    unsigned firstPhys = 0;
    unsigned long off;
    unsigned long pageSize = (unsigned long)PAGE_SIZE;
    IOReturn r;
    int gaps = 0;
    int probes = 0;

    if (!dmaRingTestEnabled)
        return;

    virt = IOMallocLow((int)OSMGA_DMA_RING_BYTES);
    if (virt == 0) {
        IOLog("OpenStepMGA D1-0: FAIL -- IOMallocLow returned 0 "
              "(conventional-memory arena exhausted or size refused)\n");
        return;
    }

    r = IOPhysicalFromVirtual(IOVmTaskSelf(), (vm_address_t)virt, &phys);
    if (r != IO_R_SUCCESS) {
        IOLog("OpenStepMGA D1-0: FAIL -- IOPhysicalFromVirtual r=%d\n", (int)r);
        IOFreeLow(virt, (int)OSMGA_DMA_RING_BYTES);
        return;
    }
    firstPhys = phys;

    /* V0-1: alignment.  The low two bits of PRIMADDRESS/PRIMEND carry mode
     * and access flags, so the ring must be at least dword aligned; report
     * the largest power-of-two alignment we actually got. */
    if ((phys & 3U) != 0U)
        IOLog("OpenStepMGA D1-0: WARNING -- phys %08x is not dword "
              "aligned\n", phys);

    /* V0-2: does it land where the arena analysis says it should? */
    if (phys >= (unsigned)OSMGA_CONVENTIONAL_END)
        IOLog("OpenStepMGA D1-0: NOTE -- phys %08x is NOT below %08x; the "
              "conventional-memory arena reading is wrong, re-analyse before "
              "D1-1\n", phys, (unsigned)OSMGA_CONVENTIONAL_END);

    /* V0-3: contiguity, measured rather than assumed. */
    for (off = pageSize; off < OSMGA_DMA_RING_BYTES; off += pageSize) {
        unsigned p = 0;

        probes++;
        r = IOPhysicalFromVirtual(IOVmTaskSelf(),
                                  (vm_address_t)((char *)virt + off), &p);
        if (r != IO_R_SUCCESS) {
            IOLog("OpenStepMGA D1-0: FAIL -- no mapping at +%u (r=%d)\n",
                  (unsigned)off, (int)r);
            gaps++;
            break;
        }
        if (p != firstPhys + (unsigned)off) {
            IOLog("OpenStepMGA D1-0: FAIL -- discontiguous at +%u: "
                  "expected %08x got %08x\n",
                  (unsigned)off, firstPhys + (unsigned)off, p);
            gaps++;
            break;
        }
    }

    if (gaps == 0)
        IOLog("OpenStepMGA D1-0: PASS %08x..%08x contiguous (%d probes)\n",
              firstPhys, firstPhys + (unsigned)OSMGA_DMA_RING_BYTES - 1U,
              probes);
    else
        IOLog("OpenStepMGA D1-0: FAIL -- contiguity broken; do not proceed "
              "to D1-1\n");

    IOFreeLow(virt, (int)OSMGA_DMA_RING_BYTES);
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

    statTransferTable++;

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
    int claimed = [self claimEngineForMode];

    statRevertVGA++;
    linearModeActive = NO;
    [super revertToVGAMode];
    [self releaseEngineAfterMode:claimed];
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
    /* The character device outlives us by design (S4a), so the ioctl handler
     * can still be entered after this.  Drop the receiver rather than let it
     * message freed memory; the handler answers ENXIO, the probe reads that
     * as "not our driver", and the caller renders in software. */
    if (osmgaCapsInstance == self)
        osmgaCapsInstance = nil;
    [self teardownMappings];
    linearModeActive = NO;
    configuredVideoMemoryBytes = 0;
    return [super free];
}


/*
 * Build the S1 solid-fill list.  Shared by D1-1 (assemble and dump) and
 * D1-2 (assemble and start) on purpose: the negative control in D1-2 is
 * only worth anything if both paths produce the same bytes and differ
 * solely in whether PRIMEND is written.
 *
 * OPMODE is absent because it cannot be encoded (osmgaDmaIndex refuses it);
 * the caller sets it over MMIO first.
 */
static int
osmgaDmaBuildFillList(unsigned long *ring, unsigned long ringDwords,
                      unsigned long stride, unsigned long blockY,
                      unsigned long *outTailDwords,
                      unsigned long *outTotalDwords)
{
    unsigned long pos = 0;
    unsigned long start = blockY * stride;
    unsigned long end   = (blockY + OSMGA_S1_H - 1UL) * stride;
    int ok = 1;

    ok = ok && osmgaDmaBlock(ring, ringDwords, &pos,
                             MGA_PITCH,   stride,
                             MGA_YDSTORG, 0UL,
                             MGA_MACCESS, MGA_MACCESS_PW32,
                             MGA_PLNWT,   0xffffffffUL);
    ok = ok && osmgaDmaBlock(ring, ringDwords, &pos,
                             MGA_FCOL,    0UL,
                             MGA_BCOL,    0UL,
                             MGA_CXBNDRY,
                                 (((OSMGA_S1_X + OSMGA_S1_W - 1UL) << 16) |
                                  OSMGA_S1_X),
                             MGA_YTOP,    start);
    ok = ok && osmgaDmaBlock(ring, ringDwords, &pos,
                             MGA_YBOT,    end,
                             MGA_SRCORG,  0UL,
                             MGA_DSTORG,  0UL,
                             MGA_DWGCTL,  MGA_DWGCTL_SOLID_FILL);
    ok = ok && osmgaDmaBlock(ring, ringDwords, &pos,
                             MGA_FCOL,    OSMGA_S1_FILL,
                             MGA_FXBNDRY,
                                 (((OSMGA_S1_X + OSMGA_S1_W) << 16) |
                                  (OSMGA_S1_X & 0xffffUL)),
                             MGA_YDSTLEN + MGA_EXEC,
                                 ((blockY << 16) | OSMGA_S1_H),
                             MGA_DMAPAD,  0UL);
    ok = ok && osmgaDmaBlock(ring, ringDwords, &pos,
                             MGA_DMAPAD,  0UL,
                             MGA_DMAPAD,  0UL,
                             MGA_DMAPAD,  0UL,
                             MGA_SOFTRAP, 0UL);
    if (!ok)
        return 0;

    *outTailDwords = pos;          /* PRIMEND points here */

    /* The card reads past PRIMEND, so leave it a padding block to read. */
    if (!osmgaDmaBlock(ring, ringDwords, &pos,
                       MGA_DMAPAD, 0UL, MGA_DMAPAD, 0UL,
                       MGA_DMAPAD, 0UL, MGA_DMAPAD, 0UL))
        return 0;

    *outTotalDwords = pos;
    return 1;
}

/*
 * D1-1 -- build the list that reproduces the S1 solid fill, and stop.
 *
 * PRIMADDRESS/PRIMEND are not written, so the card never learns the list
 * exists.  The whole point is to get the encoding wrong on a workbench
 * rather than in a DMA engine: the list is dumped dword by dword so it can
 * be decoded by hand against the register table in the plan.
 *
 * Layout (docs/D1_PRIMARY_DMA_RING_PLAN.md 1-6):
 *
 *   B0..B3   drawing state and the fill, ending in YDSTLEN|EXEC
 *   B4       SOFTRAP alone in its own block, as the DRM emits it
 *   <- PRIMEND would point here
 *   B5       one DMAPAD block, because the card reads past PRIMEND
 *
 * The trailing pad is not decoration.  The DRM pads for the same reason and
 * cites the G400 manual: the card partially reads the command after the
 * end.  Without it the card would decode whatever follows the list.
 */
- (void)runDmaRingBuildTest
{
    void *virt;
    unsigned phys = 0;
    unsigned long *ring;
    unsigned long ringDwords = OSMGA_DMA_RING_BYTES / 4UL;
    unsigned long pos = 0;
    unsigned long tailDwords;
    IODisplayInfo *di = [self displayInfo];
    unsigned long stride = (unsigned long)di->rowBytes / 4UL;
    unsigned long blockY = (unsigned long)di->height + OSMGA_S1_GUARD_ROWS;
    IOReturn r;

    if (!dmaRingTestEnabled)
        return;

    virt = IOMallocLow((int)OSMGA_DMA_RING_BYTES);
    if (virt == 0) {
        IOLog("OpenStepMGA D1-1: FAIL -- IOMallocLow returned 0\n");
        return;
    }
    r = IOPhysicalFromVirtual(IOVmTaskSelf(), (vm_address_t)virt, &phys);
    if (r != IO_R_SUCCESS) {
        IOLog("OpenStepMGA D1-1: FAIL -- IOPhysicalFromVirtual r=%d\n", (int)r);
        IOFreeLow(virt, (int)OSMGA_DMA_RING_BYTES);
        return;
    }
    ring = (unsigned long *)virt;

    if (!osmgaDmaBuildFillList(ring, ringDwords, stride, blockY,
                               &tailDwords, &pos)) {
        IOLog("OpenStepMGA D1-1: FAIL -- list assembly rejected\n");
        IOFreeLow(virt, (int)OSMGA_DMA_RING_BYTES);
        return;
    }

    IOLog("OpenStepMGA D1-1: %u blocks, tail=+%u, padded to +%u\n",
          (unsigned)(pos / OSMGA_DMA_BLOCK_DWORDS),
          (unsigned)(tailDwords * 4UL), (unsigned)(pos * 4UL));

    /* The block-by-block dump that this step existed to produce has served
     * its purpose -- the encoding was decoded by hand and matched.  Keeping
     * it costs six log lines every boot, and the log drops bursts. */

    /* V1-3: the assembler must refuse OPMODE rather than mis-encode it. */
    {
        unsigned long scratch = pos;

        if (osmgaDmaBlock(ring, ringDwords, &scratch,
                          MGA_OPMODE, 0UL, MGA_DMAPAD, 0UL,
                          MGA_DMAPAD, 0UL, MGA_DMAPAD, 0UL))
            IOLog("OpenStepMGA D1-1: FAIL -- OPMODE was accepted; the "
                  "whitelist does not work\n");
    }

    /* V1-2: index arithmetic against the values worked out by hand. */
    IOFreeLow(virt, (int)OSMGA_DMA_RING_BYTES);
}


/*
 * D1-2 -- start the ring.  First hardware contact for DMA.
 *
 * Everything before this step could not fail destructively.  This one can:
 * writing PRIMEND hands the card a physical address and lets it fetch
 * commands out of system memory on its own.  Three things contain it, in
 * order of how much they are relied on:
 *
 *   1. the assembler's whitelist -- no index can encode PRIMADDRESS,
 *      PRIMEND or OPMODE, so a list cannot reprogram the engine reading it
 *   2. the clip registers -- but only while the list is well formed, since
 *      DWGCTL bit 31 would switch clipping off
 *   3. the offscreen bound -- the block sits inside the 7 MiB of VRAM that
 *      the 1600x1200x32 scanout proved real, above the visible area by a
 *      256-row guard
 *
 * The negative control matters as much as the positive result.  Writing
 * PRIMADDRESS alone must leave the destination untouched; only then does
 * finding the fill afterwards mean the card fetched and executed the list,
 * rather than something else having written those pixels.  Both halves run
 * the same builder and the same setup and differ in one register write.
 */
- (void)runDmaRingStartTest
{
    IODisplayInfo *di = [self displayInfo];
    const OSMGAFormat *f = &osmgaFmt[selectedFormatIndex];
    vm_address_t base = mmioBase;
    unsigned long stride, blockY, byteStart, byteEnd;
    unsigned long tailDwords = 0, totalDwords = 0;
    unsigned long ringDwords = OSMGA_DMA_RING_BYTES / 4UL;
    unsigned long *ring;
    void *virt;
    unsigned phys = 0, primBefore = 0, primAfter = 0;
    unsigned long status;
    vm_address_t alias = 0;
    unsigned long mapLen = 0;
    volatile unsigned long *blk = 0;
    unsigned long row, col, mismatches = 0UL, checksum = 0UL;
    unsigned long spins;
    IOReturn r;
    int started = 0;

    if (!dmaRingTestEnabled || !linearModeActive || !mmioMapped)
        return;
    if (f->bytesPerPixel != 4) {
        IOLog("OpenStepMGA D1-2: not a 32bpp mode, skipped\n");
        return;
    }

    stride    = (unsigned long)di->rowBytes / 4UL;
    blockY    = (unsigned long)di->height + OSMGA_S1_GUARD_ROWS;
    byteStart = blockY * stride * 4UL;
    byteEnd   = byteStart + (OSMGA_S1_H - 1UL) * stride * 4UL +
                OSMGA_S1_W * 4UL;

    if (byteEnd > OSMGA_S1_VRAM_PROVEN) {
        IOLog("OpenStepMGA D1-2: block ends at %lu, past the %lu proven "
              "VRAM bound, skipped\n", byteEnd, OSMGA_S1_VRAM_PROVEN);
        return;
    }

    virt = IOMallocLow((int)OSMGA_DMA_RING_BYTES);
    if (virt == 0) {
        IOLog("OpenStepMGA D1-2: FAIL -- IOMallocLow returned 0\n");
        return;
    }
    r = IOPhysicalFromVirtual(IOVmTaskSelf(), (vm_address_t)virt, &phys);
    if (r != IO_R_SUCCESS) {
        IOLog("OpenStepMGA D1-2: FAIL -- IOPhysicalFromVirtual r=%d\n",
              (int)r);
        IOFreeLow(virt, (int)OSMGA_DMA_RING_BYTES);
        return;
    }
    ring = (unsigned long *)virt;

    if (!osmgaDmaBuildFillList(ring, ringDwords, stride, blockY,
                               &tailDwords, &totalDwords)) {
        IOLog("OpenStepMGA D1-2: FAIL -- list assembly rejected\n");
        IOFreeLow(virt, (int)OSMGA_DMA_RING_BYTES);
        return;
    }
    if ((phys & 3U) != 0U || ((phys + tailDwords * 4UL) & 3UL) != 0UL) {
        IOLog("OpenStepMGA D1-2: FAIL -- ring not dword aligned "
              "(phys=%08x tail=%08x)\n",
              phys, phys + (unsigned)(tailDwords * 4UL));
        IOFreeLow(virt, (int)OSMGA_DMA_RING_BYTES);
        return;
    }

    r = osmgaMapUncachedBlock(frameBufferPhysical, byteStart, byteEnd,
                              &alias, &mapLen, &blk);
    if (r != IO_R_SUCCESS) {
        IOLog("OpenStepMGA D1-2: uncached alias map failed r=%d, skipped\n",
              (int)r);
        IOFreeLow(virt, (int)OSMGA_DMA_RING_BYTES);
        return;
    }

    if (!osmgaStormWaitIdle(base)) {
        IOLog("OpenStepMGA D1-2: engine BUSY at entry (timeout), aborted\n");
        goto unmap;
    }

    /* OPMODE cannot be expressed in the list (it sits between the two DMA
     * windows), so it is set here, before the card reads anything. */
    {
        unsigned long opmode = osmgaR32(base, MGA_OPMODE);

        osmgaW32(base, MGA_OPMODE,
                 MGA_OPMODE_DMA_BLIT |
                 (opmode & ~(unsigned long)MGA_OPMODE_BYTESWAP));
    }

    /* Sentinel, through the uncached alias, exactly as S1 does: it proves
     * the readback path works and rules out the destination already
     * holding the fill value. */
    for (row = 0UL; row < OSMGA_S1_H; row++)
        for (col = 0UL; col < OSMGA_S1_W; col++)
            blk[row * stride + col] = OSMGA_S1_SENTINEL;
    for (row = 0UL; row < OSMGA_S1_H; row++)
        for (col = 0UL; col < OSMGA_S1_W; col++)
            if (blk[row * stride + col] != OSMGA_S1_SENTINEL)
                mismatches++;
    if (mismatches != 0UL) {
        IOLog("OpenStepMGA D1-2: sentinel readback failed (%lu bad), "
              "aborted\n", mismatches);
        goto unmap;
    }

    /* Any SOFTRAP left over from earlier would be read as our completion. */
    osmgaW32(base, MGA_ICLEAR, MGA_SOFTRAPICLR);

    primBefore = (unsigned)osmgaR32(base, MGA_PRIMADDRESS);

    /*
     * Negative control.  PRIMADDRESS on its own selects the list and the
     * transfer mode; it does not start anything -- the DRM writes it at
     * init and starts later by writing PRIMEND.  So after this the block
     * must still be sentinel.
     */
    osmgaW32(base, MGA_PRIMADDRESS, (unsigned long)phys | MGA_DMA_GENERAL);
    for (spins = 0UL; spins < OSMGA_S1_SPIN_LIMIT / 10UL; spins++)
        (void)osmgaR32(base, MGA_ENGSTATUS);

    mismatches = 0UL;
    for (row = 0UL; row < OSMGA_S1_H; row++)
        for (col = 0UL; col < OSMGA_S1_W; col++)
            if (blk[row * stride + col] != OSMGA_S1_SENTINEL)
                mismatches++;
    if (mismatches != 0UL) {
        IOLog("OpenStepMGA D1-2: NEGATIVE CONTROL FAILED -- %lu pixels "
              "changed with only PRIMADDRESS written; a later positive "
              "result would prove nothing.  Aborted.\n", mismatches);
        goto unmap;
    }
    /* Negative control passed: PRIMADDRESS alone wrote nothing. */

    /*
     * Make sure the list is in memory before the card is told to fetch it.
     * Reading the ring back forces the stores out of the CPU's view, and
     * the MMIO read that follows is uncached, so it cannot be reordered
     * ahead of them from the device's side either.
     */
    {
        unsigned long sum = 0UL;
        unsigned long i;

        for (i = 0UL; i < totalDwords; i++)
            sum += ring[i];
        (void)osmgaR32(base, MGA_ENGSTATUS);
        if (sum == 0xFFFFFFFFUL)      /* never true; keeps sum live */
            IOLog("OpenStepMGA D1-2: barrier %lu\n", sum);
    }

    /* PCI: neither PRIMNOSTART nor PAGPXFER.  This starts the fetch. */
    started = 1;
    osmgaW32(base, MGA_PRIMEND,
             ((unsigned long)phys + tailDwords * 4UL) |
             MGA_DMA_GENERAL);

    for (spins = 0UL; spins < OSMGA_S1_SPIN_LIMIT; spins++) {
        status = osmgaR32(base, MGA_ENGSTATUS);
        if ((status & MGA_DMA_DONE_MASK) == MGA_DMA_DONE_VALUE)
            break;
    }
    primAfter = (unsigned)osmgaR32(base, MGA_PRIMADDRESS);
    status = osmgaR32(base, MGA_ENGSTATUS);
    osmgaW32(base, MGA_ICLEAR, MGA_SOFTRAPICLR);   /* consume our trap */

    if (spins >= OSMGA_S1_SPIN_LIMIT) {
        IOLog("OpenStepMGA D1-2: FAIL -- DMA did not report idle "
              "(timeout).  Acceleration stays off and the ring buffer is "
              "NOT released; the engine may still be reading it.\n");
        goto keepbuffer;
    }

    for (row = 0UL; row < OSMGA_S1_H; row++) {
        for (col = 0UL; col < OSMGA_S1_W; col++) {
            unsigned long v = blk[row * stride + col];

            checksum += v;
            if (v != OSMGA_S1_FILL)
                mismatches++;
        }
    }

    if (mismatches == 0UL && checksum == 0xDBEEF000UL) {
        IOLog("OpenStepMGA D1-2: PASS neg+start, PRIMADDRESS %08x->%08x, "
              "checksum %08lx, spins=%lu\n",
              primBefore, primAfter, checksum, spins);
    } else {
        IOLog("OpenStepMGA D1-2: FAIL -- %lu mismatched px, checksum %08lx "
              "(expected %08lx)\n", mismatches, checksum, 0xDBEEF000UL);
        goto keepbuffer;
    }

    /*
     * Release only with the engine demonstrably done with the buffer:
     * the read pointer reached the tail we published and both the DMA and
     * the drawing engine report idle.  IOMallocLow's contract says nothing
     * about device synchronisation, so anything short of that keeps the
     * memory for the rest of the boot.
     */
    if (primAfter == phys + (unsigned)(tailDwords * 4UL) &&
        (status & MGA_DMA_DONE_MASK) == MGA_DMA_DONE_VALUE) {
        IOFreeLow(virt, (int)OSMGA_DMA_RING_BYTES);
    } else {
        IOLog("OpenStepMGA D1-2: ring KEPT for this boot (quiescence not "
              "provable)\n");
    }
    goto unmap;

keepbuffer:
    IOLog("OpenStepMGA D1-2: ring KEPT at %08x for this boot\n", phys);

unmap:
    if (!started && virt != 0)
        IOFreeLow(virt, (int)OSMGA_DMA_RING_BYTES);
    if (alias != 0)
        (void)IOUnmapPhysicalFromIOTask(alias, mapLen);
}


/*
 * D2-0a -- does the WARP pipe accept configuration?
 *
 * The 3D counterpart of S1.  Two registers written, one read back: WMISC
 * is the only hard pass/fail signal the DRM itself uses, and 0x3 is its
 * expected value even though 0xB is what gets written.  No microcode, no
 * DMA, no triangle -- if this fails there is no reason to upload 11,610
 * lines of microcode.
 *
 * WIADDR2 is set to WMODE_SUSPEND before and after, so the pipe is
 * standing still on both sides of the probe.  The three remaining WARP
 * registers (WGETMSB, WVRTXSZ, WACCEPTSEQ) are deliberately untouched:
 * nothing in the sources ever reads them back, so their read semantics
 * are unproven and there is no need to find out here.
 *
 * Deviation worth remembering: the DRM installs microcode before calling
 * warp_init.  We do not.  mga_warp_init never looks at the microcode
 * buffer and leaves the pipe suspended, so the readback should stand on
 * its own -- but if it does not, "the order differs" is the first
 * suspicion, not "there is no WARP".
 */
- (void)runWarpConfigTest
{
    vm_address_t base = mmioBase;
    unsigned long wmiscBefore, wiaddr2Before, wmiscAfter, wiaddr2After;

    if (!warpTestEnabled || !linearModeActive || !mmioMapped)
        return;

    if (!osmgaStormWaitIdle(base)) {
        IOLog("OpenStepMGA D2-0a: 2D engine BUSY at entry (timeout), "
              "aborted\n");
        return;
    }

    /* Logged for the record only.  Never written back -- see the note on
     * MGA_WIADDR2 above. */
    wmiscBefore   = osmgaR32(base, MGA_WMISC);
    wiaddr2Before = osmgaR32(base, MGA_WIADDR2);
    osmgaW32(base, MGA_WIADDR2, MGA_WMODE_SUSPEND);
    osmgaW32(base, MGA_WMISC, MGA_WMISC_WRITE);
    wmiscAfter = osmgaR32(base, MGA_WMISC);
    osmgaW32(base, MGA_WIADDR2, MGA_WMODE_SUSPEND);
    wiaddr2After = osmgaR32(base, MGA_WIADDR2);

    if (wmiscAfter == MGA_WMISC_EXPECTED)
        IOLog("OpenStepMGA D2-0a: PASS (WMISC %08lx -> %08lx, WIADDR2 was "
              "%08lx)\n", wmiscBefore, wmiscAfter, wiaddr2Before);
    else
        IOLog("OpenStepMGA D2-0a: FAIL -- WMISC read %08lx.  Do not upload "
              "microcode; suspect the ordering deviation first (the DRM "
              "installs microcode before warp_init)\n", wmiscAfter);

    if (wiaddr2After != MGA_WMODE_SUSPEND)
        IOLog("OpenStepMGA D2-0a: WARNING -- WIADDR2 reads %08lx, not "
              "suspended\n", wiaddr2After);

    if (wmiscAfter != MGA_WMISC_EXPECTED)
        return;

    /*
     * D2-0b -- the full mga_warp_init sequence, run only because D2-0a
     * just passed.  Order is the DRM's: suspend the pipe, set the three
     * configuration registers, then enable WMISC and check it again.
     *
     * These three registers are the ones nothing in the sources ever reads
     * back, so there is no readback to check them with and no attempt is
     * made to invent one.  The end state is configured and suspended,
     * which is what D2-1 will need.
     */
    osmgaW32(base, MGA_WIADDR2,    MGA_WMODE_SUSPEND);
    osmgaW32(base, MGA_WGETMSB,    MGA_WGETMSB_G400);
    osmgaW32(base, MGA_WVRTXSZ,    MGA_WVRTXSZ_G400);
    osmgaW32(base, MGA_WACCEPTSEQ, MGA_WACCEPTSEQ_G400);
    osmgaW32(base, MGA_WMISC,      MGA_WMISC_WRITE);

    wmiscAfter   = osmgaR32(base, MGA_WMISC);
    wiaddr2After = osmgaR32(base, MGA_WIADDR2);

    if (wmiscAfter == MGA_WMISC_EXPECTED &&
        wiaddr2After == MGA_WMODE_SUSPEND)
        IOLog("OpenStepMGA D2-0b: PASS (configured and suspended)\n");
    else
        IOLog("OpenStepMGA D2-0b: FAIL -- WMISC=%08lx WIADDR2=%08lx; do not "
              "upload microcode\n", wmiscAfter, wiaddr2After);
}


/*
 * Place the sixteen G400 pipes in a fresh low-memory buffer and fill in
 * their physical addresses.  Shared by D2-1b, which verifies the layout
 * and then frees, and by D2-2a, which keeps it because the card will be
 * pointed at it -- so the code that D2-1b proved correct is the same code
 * that runs when it matters.
 *
 * Offsets accumulate by each pipe's own rounded size.  Pipes 0-7 round to
 * 1024 and 8-15 to 1280, so an address scaled from the index would be
 * wrong from pipe 8 on, and wrong in a way alignment and range checks
 * cannot see.
 *
 * On failure the buffer is released and 0 is returned; on success the
 * caller owns it.
 */
static int
osmgaWarpPlaceUcode(unsigned long *pipePhys, unsigned long *pipeOff,
                    void **outVirt, unsigned *outPhys,
                    unsigned long *outAllocBytes)
{
    unsigned long pageSize = (unsigned long)PAGE_SIZE;
    unsigned long allocBytes =
        (OSMGA_WARP_TOTAL_BYTES + pageSize - 1UL) & ~(pageSize - 1UL);
    unsigned long off = 0UL;
    unsigned long i, j, bad = 0UL;
    unsigned char *buf;
    void *virt;
    unsigned phys = 0;
    IOReturn r;

    virt = IOMallocLow((int)allocBytes);
    if (virt == 0) {
        IOLog("OpenStepMGA warp: IOMallocLow(%lu) returned 0\n", allocBytes);
        return 0;
    }
    r = IOPhysicalFromVirtual(IOVmTaskSelf(), (vm_address_t)virt, &phys);
    if (r != IO_R_SUCCESS) {
        IOLog("OpenStepMGA warp: IOPhysicalFromVirtual r=%d\n", (int)r);
        IOFreeLow(virt, (int)allocBytes);
        return 0;
    }
    buf = (unsigned char *)virt;

    for (i = 0UL; i < OSMGA_WARP_PIPES; i++) {
        const OSMGAWarpPipe *p = &osmgaWarpG400Pipes[i];
        unsigned long rounded =
            (p->length / OSMGA_WARP_ALIGN + 1UL) * OSMGA_WARP_ALIGN;

        if (off + rounded > allocBytes) {
            IOLog("OpenStepMGA warp: pipe %lu overruns the buffer\n", i);
            IOFreeLow(virt, (int)allocBytes);
            return 0;
        }
        for (j = 0UL; j < p->length; j++)
            buf[off + j] = p->bytes[j];
        for (; j < rounded; j++)
            buf[off + j] = 0;
        pipeOff[i]  = off;
        pipePhys[i] = (unsigned long)phys + off;
        off += rounded;
    }

    for (i = 0UL; i < OSMGA_WARP_PIPES; i++) {
        const OSMGAWarpPipe *p = &osmgaWarpG400Pipes[i];

        for (j = 0UL; j < p->length; j++)
            if (buf[pipeOff[i] + j] != p->bytes[j])
                bad++;
    }
    if (bad != 0UL) {
        IOLog("OpenStepMGA warp: %lu bytes differ after copy\n", bad);
        IOFreeLow(virt, (int)allocBytes);
        return 0;
    }

    *outVirt = virt;
    *outPhys = phys;
    *outAllocBytes = allocBytes;
    return 1;
}

/*
 * Assemble the G400 single-texture pipe emit (mga_g400_emit_pipe with the
 * T2 flush branch not taken, which is the case from a cold start).
 *
 * `startPhys` non-zero starts the pipe; zero leaves the final WIADDR2 at
 * WMODE_SUSPEND.  That is the negative control: identical encoding and
 * ordering, one value different, so a difference in outcome can only come
 * from having pointed WARP at microcode.
 *
 * When the pipe is started, a suspend block follows it before the SOFTRAP.
 * That does not promise to rescue a wedged WARP; it keeps the intended run
 * short if the command parser carries on regardless.
 */
static int
osmgaDmaBuildPipeList(unsigned long *ring, unsigned long ringDwords,
                      unsigned long startPhys,
                      unsigned long *outTailDwords,
                      unsigned long *outTotalDwords)
{
    unsigned long pos = 0UL;
    int ok = 1;

    ok = ok && osmgaDmaBlock(ring, ringDwords, &pos,
                             MGA_WIADDR2, MGA_WMODE_SUSPEND,
                             MGA_DMAPAD, 0UL, MGA_DMAPAD, 0UL,
                             MGA_DMAPAD, 0UL);
    ok = ok && osmgaDmaBlock(ring, ringDwords, &pos,
                             MGA_WVRTXSZ, MGA_WVRTXSZ_G400,
                             MGA_DMAPAD, 0UL, MGA_DMAPAD, 0UL,
                             MGA_DMAPAD, 0UL);
    ok = ok && osmgaDmaBlock(ring, ringDwords, &pos,
                             MGA_WACCEPTSEQ, 0UL,
                             MGA_WACCEPTSEQ, 0UL,
                             MGA_WACCEPTSEQ, 0UL,
                             MGA_WACCEPTSEQ, MGA_WACCEPTSEQ_G400);
    ok = ok && osmgaDmaBlock(ring, ringDwords, &pos,
                             MGA_WFLAG,  0UL,
                             MGA_WFLAG1, 0UL,
                             MGA_WR56,   MGA_G400_WR56_MAGIC,
                             MGA_DMAPAD, 0UL);
    ok = ok && osmgaDmaBlock(ring, ringDwords, &pos,
                             MGA_WR49, 0UL, MGA_WR57, 0UL,
                             MGA_WR53, 0UL, MGA_WR61, 0UL);
    ok = ok && osmgaDmaBlock(ring, ringDwords, &pos,
                             MGA_WR54, MGA_G400_WR_MAGIC,
                             MGA_WR62, MGA_G400_WR_MAGIC,
                             MGA_WR52, MGA_G400_WR_MAGIC,
                             MGA_WR60, MGA_G400_WR_MAGIC);

    /* The three 0xffffffff pads are the DRM's documented workaround for a
     * hardware bug, and are not the same thing as the tail padding past
     * PRIMEND that D1 needs. */
    ok = ok && osmgaDmaBlock(ring, ringDwords, &pos,
                             MGA_DMAPAD, 0xffffffffUL,
                             MGA_DMAPAD, 0xffffffffUL,
                             MGA_DMAPAD, 0xffffffffUL,
                             MGA_WIADDR2,
                                 startPhys != 0UL
                                     ? (startPhys | MGA_WMODE_START)
                                     : MGA_WMODE_SUSPEND);

    if (startPhys != 0UL)
        ok = ok && osmgaDmaBlock(ring, ringDwords, &pos,
                                 MGA_WIADDR2, MGA_WMODE_SUSPEND,
                                 MGA_DMAPAD, 0UL, MGA_DMAPAD, 0UL,
                                 MGA_DMAPAD, 0UL);

    ok = ok && osmgaDmaBlock(ring, ringDwords, &pos,
                             MGA_DMAPAD, 0UL, MGA_DMAPAD, 0UL,
                             MGA_DMAPAD, 0UL, MGA_SOFTRAP, 0UL);
    if (!ok)
        return 0;

    *outTailDwords = pos;

    if (!osmgaDmaBlock(ring, ringDwords, &pos,
                       MGA_DMAPAD, 0UL, MGA_DMAPAD, 0UL,
                       MGA_DMAPAD, 0UL, MGA_DMAPAD, 0UL))
        return 0;

    *outTotalDwords = pos;
    return 1;
}

/*
 * D2-1b -- place the WARP microcode where the card can fetch it.
 *
 * On a PCI card the microcode does not go into VRAM: the DRM maps it
 * _DRM_CONSISTENT and _DRM_READ_ONLY at 256-byte alignment, which is the
 * same contiguous-system-memory arrangement D1 already proved for the
 * command ring.  So this reuses IOMallocLow and IOPhysicalFromVirtual and
 * asks only whether the bytes stay put.
 *
 * No pipe is started.  WIADDR2 is never given WMODE_START here, which is
 * why the buffer can be released at the end -- D2-2 will have to keep it,
 * since the hardware reads microcode for as long as a pipe runs.
 *
 * Two things about the layout are easy to get wrong.  Pipe numbers are a
 * bit combination (F=1 A=2 S=4 T2=8), and although the install order runs
 * 0..15, the block sizes do not: 1024 bytes for pipes 0-7, 1280 for 8-15.
 * An address computed as base + index * size would be wrong from pipe 8
 * on, so offsets accumulate instead.
 */
- (void)runWarpUcodePlacementTest
{
    vm_address_t base = mmioBase;
    unsigned long allocBytes;
    unsigned long pageSize = (unsigned long)PAGE_SIZE;
    unsigned long pipePhys[OSMGA_WARP_PIPES];
    unsigned long pipeOff[OSMGA_WARP_PIPES];
    unsigned char *buf;
    void *virt;
    unsigned phys = 0;
    unsigned long off = 0UL;
    unsigned long i, j;
    unsigned long bad = 0UL;
    IOReturn r;

    if (!warpTestEnabled || !linearModeActive || !mmioMapped)
        return;

    /* The DRM rounds the microcode area up to a page; this kernel's page
     * is 8192, not 4096 (S4a). */
    allocBytes = (OSMGA_WARP_TOTAL_BYTES + pageSize - 1UL) & ~(pageSize - 1UL);

    virt = IOMallocLow((int)allocBytes);
    if (virt == 0) {
        IOLog("OpenStepMGA D2-1b: FAIL -- IOMallocLow returned 0.  The "
              "conventional-memory arena did not have a second block; the "
              "fallback is to share one buffer with the DMA ring\n");
        return;
    }
    r = IOPhysicalFromVirtual(IOVmTaskSelf(), (vm_address_t)virt, &phys);
    if (r != IO_R_SUCCESS) {
        IOLog("OpenStepMGA D2-1b: FAIL -- IOPhysicalFromVirtual r=%d\n",
              (int)r);
        IOFreeLow(virt, (int)allocBytes);
        return;
    }
    buf = (unsigned char *)virt;
    /* Copy in pipe-index order, accumulating the offset by each pipe's own
     * rounded size.  Same arithmetic as the DRM's WARP_UCODE_INSTALL. */
    for (i = 0UL; i < OSMGA_WARP_PIPES; i++) {
        const OSMGAWarpPipe *p = &osmgaWarpG400Pipes[i];
        unsigned long rounded =
            (p->length / OSMGA_WARP_ALIGN + 1UL) * OSMGA_WARP_ALIGN;

        if (off + rounded > allocBytes) {
            IOLog("OpenStepMGA D2-1b: FAIL -- pipe %lu overruns the "
                  "buffer at offset %lu\n", i, off);
            IOFreeLow(virt, (int)allocBytes);
            return;
        }
        for (j = 0UL; j < p->length; j++)
            buf[off + j] = p->bytes[j];
        for (; j < rounded; j++)
            buf[off + j] = 0;

        pipeOff[i]  = off;
        pipePhys[i] = (unsigned long)phys + off;
        off += rounded;
    }
    /* V2-2: byte-for-byte, not a checksum. */
    for (i = 0UL; i < OSMGA_WARP_PIPES; i++) {
        const OSMGAWarpPipe *p = &osmgaWarpG400Pipes[i];

        for (j = 0UL; j < p->length; j++)
            if (buf[pipeOff[i] + j] != p->bytes[j])
                bad++;
    }
    if (bad != 0UL) {
        IOLog("OpenStepMGA D2-1b: FAIL -- %lu bytes differ after copy\n",
              bad);
        IOFreeLow(virt, (int)allocBytes);
        return;
    }
    /* V2-3: alignment, ordering, spacing.  The spacing check is the point
     * -- it is what would catch an index-scaled address calculation. */
    for (i = 0UL; i < OSMGA_WARP_PIPES; i++) {
        if ((pipePhys[i] & (OSMGA_WARP_ALIGN - 1UL)) != 0UL) {
            IOLog("OpenStepMGA D2-1b: FAIL -- pipe %lu at %08lx is not "
                  "%u-aligned\n", i, pipePhys[i], (unsigned)OSMGA_WARP_ALIGN);
            bad++;
        }
        if (i > 0UL && pipePhys[i] <= pipePhys[i - 1UL]) {
            IOLog("OpenStepMGA D2-1b: FAIL -- pipe %lu address does not "
                  "advance\n", i);
            bad++;
        }
    }
    for (i = 1UL; i < OSMGA_WARP_PIPES; i++) {
        unsigned long gap = pipePhys[i] - pipePhys[i - 1UL];
        unsigned long want = (i - 1UL) < 8UL ? 1024UL : 1280UL;

        if (gap != want) {
            IOLog("OpenStepMGA D2-1b: FAIL -- gap %lu..%lu is %lu, "
                  "expected %lu\n", i - 1UL, i, gap, want);
            bad++;
        }
    }

    /* The pipe-address table was dumped and checked by hand once; the gap
     * assertions above now carry that check every boot without the four
     * log lines. */

    /* V2-4: contiguity, measured the same way D1-0 measured it. */
    for (off = pageSize; off < allocBytes; off += pageSize) {
        unsigned p2 = 0;

        r = IOPhysicalFromVirtual(IOVmTaskSelf(),
                                  (vm_address_t)(buf + off), &p2);
        if (r != IO_R_SUCCESS || p2 != phys + (unsigned)off) {
            IOLog("OpenStepMGA D2-1b: FAIL -- discontiguous at +%lu\n", off);
            bad++;
            break;
        }
    }

    /*
     * Re-run the WARP init sequence now that there is microcode to flush.
     * D2-0b proved the sequence settles; it could not flush a cache over
     * code that was not there yet.  This is the DRM's order: install, then
     * warp_init.
     */
    osmgaW32(base, MGA_WIADDR2,    MGA_WMODE_SUSPEND);
    osmgaW32(base, MGA_WGETMSB,    MGA_WGETMSB_G400);
    osmgaW32(base, MGA_WVRTXSZ,    MGA_WVRTXSZ_G400);
    osmgaW32(base, MGA_WACCEPTSEQ, MGA_WACCEPTSEQ_G400);
    osmgaW32(base, MGA_WMISC,      MGA_WMISC_WRITE);
    {
        unsigned long wmisc = osmgaR32(base, MGA_WMISC);

        if (wmisc != MGA_WMISC_EXPECTED) {
            IOLog("OpenStepMGA D2-1b: warp_init after placement gave "
                  "WMISC=%08lx\n", wmisc);
            bad++;
        }
    }

    if (bad == 0UL)
        IOLog("OpenStepMGA D2-1b: PASS placed at %08x (%lu bytes, %u "
              "pipes)\n", phys, off, (unsigned)OSMGA_WARP_PIPES);
    else
        IOLog("OpenStepMGA D2-1b: FAIL -- %lu checks failed\n", bad);

    /* Safe to release: no pipe was ever pointed at this memory.  D2-2 must
     * not do this. */
    IOFreeLow(virt, (int)allocBytes);
}


/*
 * D2-2a -- start a WARP pipe, with no vertices.
 *
 * The negative control runs first and must pass: the same register list
 * with WMODE_SUSPEND in place of the final START.  If that does not go
 * through, the encoding or ordering is wrong and there is no point
 * pointing WARP at microcode.  The real run then changes exactly one
 * value.
 *
 * Nothing is drawn.  What this cannot claim is that nothing *can* be
 * drawn: the DRM's vertex path is absent, but this is the first execution
 * of opaque microcode from card-visible memory, and no source says what a
 * wrong microcode/chip pairing does internally.  The plan says so too.
 *
 * The microcode buffer is not released -- the card has been given its
 * address.
 */
- (BOOL)runWarpPipeOnce:(unsigned long)startPhys
                  label:(const char *)label
             ringVirt:(unsigned long *)ring
             ringPhys:(unsigned)ringPhys
{
    vm_address_t base = mmioBase;
    unsigned long ringDwords = OSMGA_DMA_RING_BYTES / 4UL;
    unsigned long tailDwords = 0UL, totalDwords = 0UL;
    unsigned long status, spins, sum, i;
    unsigned primBefore, primAfter;

    if (!osmgaDmaBuildPipeList(ring, ringDwords, startPhys,
                               &tailDwords, &totalDwords)) {
        IOLog("OpenStepMGA D2-2a/%s: FAIL -- list assembly rejected\n", label);
        return NO;
    }

    if (!osmgaStormWaitIdle(base)) {
        IOLog("OpenStepMGA D2-2a/%s: engine BUSY at entry, aborted\n", label);
        return NO;
    }
    osmgaW32(base, MGA_ICLEAR, MGA_SOFTRAPICLR);

    sum = 0UL;
    for (i = 0UL; i < totalDwords; i++)
        sum += ring[i];
    (void)osmgaR32(base, MGA_ENGSTATUS);
    if (sum == 0xFFFFFFFFUL)
        IOLog("OpenStepMGA D2-2a: barrier %lu\n", sum);

    primBefore = (unsigned)osmgaR32(base, MGA_PRIMADDRESS);
    osmgaW32(base, MGA_PRIMADDRESS, (unsigned long)ringPhys | MGA_DMA_GENERAL);
    osmgaW32(base, MGA_PRIMEND,
             ((unsigned long)ringPhys + tailDwords * 4UL) | MGA_DMA_GENERAL);

    for (spins = 0UL; spins < OSMGA_S1_SPIN_LIMIT; spins++) {
        status = osmgaR32(base, MGA_ENGSTATUS);
        if ((status & MGA_DMA_DONE_MASK) == MGA_DMA_DONE_VALUE)
            break;
    }
    primAfter = (unsigned)osmgaR32(base, MGA_PRIMADDRESS);
    status = osmgaR32(base, MGA_ENGSTATUS);
    osmgaW32(base, MGA_ICLEAR, MGA_SOFTRAPICLR);

    IOLog("OpenStepMGA D2-2a/%s: %u blocks, PRIMADDRESS %08x -> %08x "
          "(expected %08x), STATUS=%08lx, spins=%lu\n",
          label, (unsigned)(totalDwords / OSMGA_DMA_BLOCK_DWORDS),
          primBefore, primAfter,
          ringPhys + (unsigned)(tailDwords * 4UL), status, spins);

    if (spins >= OSMGA_S1_SPIN_LIMIT) {
        IOLog("OpenStepMGA D2-2a/%s: FAIL -- DMA did not complete\n", label);
        return NO;
    }

    {
        unsigned long wmisc   = osmgaR32(base, MGA_WMISC);
        unsigned long wiaddr2 = osmgaR32(base, MGA_WIADDR2);

        /* WIADDR2's read semantics are unproven, so it is observed, never
         * used as a verdict. */
        IOLog("OpenStepMGA D2-2a/%s: WMISC=%08lx (expected %08lx), "
              "WIADDR2 reads %08lx (observation only)\n",
              label, wmisc, MGA_WMISC_EXPECTED, wiaddr2);
        if (wmisc != MGA_WMISC_EXPECTED) {
            IOLog("OpenStepMGA D2-2a/%s: FAIL -- WARP configuration "
                  "collapsed\n", label);
            return NO;
        }
    }
    return YES;
}

- (void)runWarpPipeStartTest
{
    vm_address_t base = mmioBase;
    unsigned long pipePhys[OSMGA_WARP_PIPES];
    unsigned long pipeOff[OSMGA_WARP_PIPES];
    unsigned long allocBytes = 0UL;
    void *ucodeVirt = 0;
    unsigned ucodePhys = 0;
    void *ringVirt;
    unsigned ringPhys = 0;
    IOReturn r;

    if (!warpTestEnabled || !linearModeActive || !mmioMapped)
        return;

    if (!osmgaWarpPlaceUcode(pipePhys, pipeOff, &ucodeVirt, &ucodePhys,
                             &allocBytes)) {
        IOLog("OpenStepMGA D2-2a: FAIL -- could not place microcode\n");
        return;
    }
    IOLog("OpenStepMGA D2-2a: microcode at %08x, pipe 0 at %08lx "
          "(buffer kept for this boot)\n", ucodePhys, pipePhys[0]);

    /* warp_init after placement, so its WCACHEFLUSH has code to flush. */
    osmgaW32(base, MGA_WIADDR2,    MGA_WMODE_SUSPEND);
    osmgaW32(base, MGA_WGETMSB,    MGA_WGETMSB_G400);
    osmgaW32(base, MGA_WVRTXSZ,    MGA_WVRTXSZ_G400);
    osmgaW32(base, MGA_WACCEPTSEQ, MGA_WACCEPTSEQ_G400);
    osmgaW32(base, MGA_WMISC,      MGA_WMISC_WRITE);
    if (osmgaR32(base, MGA_WMISC) != MGA_WMISC_EXPECTED) {
        IOLog("OpenStepMGA D2-2a: FAIL -- warp_init did not settle after "
              "placement\n");
        return;                      /* buffer deliberately not released */
    }

    ringVirt = IOMallocLow((int)OSMGA_DMA_RING_BYTES);
    if (ringVirt == 0) {
        IOLog("OpenStepMGA D2-2a: FAIL -- no ring buffer (arena has the "
              "microcode block already)\n");
        return;
    }
    r = IOPhysicalFromVirtual(IOVmTaskSelf(), (vm_address_t)ringVirt,
                              &ringPhys);
    if (r != IO_R_SUCCESS) {
        IOLog("OpenStepMGA D2-2a: FAIL -- ring IOPhysicalFromVirtual r=%d\n",
              (int)r);
        IOFreeLow(ringVirt, (int)OSMGA_DMA_RING_BYTES);
        return;
    }
    IOLog("OpenStepMGA D2-2a: ring at %08x\n", ringPhys);

    /* Negative control first: same list, WMODE_SUSPEND instead of START. */
    if (![self runWarpPipeOnce:0UL label:"neg"
                      ringVirt:(unsigned long *)ringVirt
                      ringPhys:ringPhys]) {
        IOLog("OpenStepMGA D2-2a: negative control FAILED -- not pointing "
              "WARP at microcode\n");
        IOFreeLow(ringVirt, (int)OSMGA_DMA_RING_BYTES);
        return;
    }
    IOLog("OpenStepMGA D2-2a: negative control ok (WARP register writes "
          "carried by DMA, pipe not started)\n");

    if ([self runWarpPipeOnce:pipePhys[0] label:"start"
                     ringVirt:(unsigned long *)ringVirt
                     ringPhys:ringPhys]) {
        osmgaW32(base, MGA_WIADDR2, MGA_WMODE_SUSPEND);
        IOLog("OpenStepMGA D2-2a: PASS -- pipe 0 started and the engine is "
              "still alive; pipe suspended again\n");
    } else {
        IOLog("OpenStepMGA D2-2a: FAIL -- do not proceed to D2-2b\n");
    }

    /* The ring can go; the microcode cannot -- the card was given its
     * address and nothing proves it has stopped reading. */
    IOFreeLow(ringVirt, (int)OSMGA_DMA_RING_BYTES);
    IOLog("OpenStepMGA D2-2a: end (ring released, microcode kept)\n");
}


/*
 * D3-0 -- does the interpolating rasteriser run?
 *
 * This is a measurement, not a pass/fail against a formula.  The DR
 * registers' fixed-point layout is not documented in any source we have --
 * mga_storm.c writes (component << 7) with zero increments, which pins
 * neither the fractional width nor which of the two increment registers is
 * the x step.  So the probe writes a known start and a single non-zero
 * increment, then prints the pixels it got.  What the numbers mean is
 * derived afterwards, on paper.
 *
 * The only verdict taken here is the one that does not need the format:
 * a constant block means interpolation did nothing.
 *
 * Geometry is S1's, unchanged and already proven -- same offscreen block,
 * same clip, same FXBNDRY/YDSTLEN.  The one thing that differs is DWGCTL's
 * atype and the DR registers, so a failure has one plausible cause.
 */
/*
 * Emit one trapezoid with the XAA edge parameters.  `dxL/dyL/eL` describe
 * the left edge, `dxR/dyR/eR` the right; `left`/`right` are the starting x
 * of each edge on the first row.  Setting both dx to zero gives vertical
 * edges, i.e. a rectangle -- which is how the control band checks that
 * clearing ARZERO/SGNZERO has not broken drawing on its own.
 *
 * DWGCTL is restored afterwards so the cleared bits cannot leak into the
 * next draw, matching what the DDX does.
 */
static void
osmgaStormTrap(vm_address_t base, unsigned long dwgctl,
               unsigned long y, unsigned long h,
               long left,  long dxL, long dyL, long eL,
               long right, long dxR, long dyR, long eR)
{
    int sdxl = (dxL < 0) ? 1 : 0;
    int sdxr = (dxR < 0) ? 1 : 0;
    long ar2 = sdxl ? dxL : -dxL;
    long ar5 = sdxr ? dxR : -dxR;

    osmgaW32(base, MGA_DWGCTL, MGA_DWGCTL_SLOPED(dwgctl));
    osmgaW32(base, MGA_AR0, (unsigned long)dyL);
    osmgaW32(base, MGA_AR1, (unsigned long)(ar2 - eL));
    osmgaW32(base, MGA_AR2, (unsigned long)ar2);
    osmgaW32(base, MGA_AR4, (unsigned long)(ar5 - eR));
    osmgaW32(base, MGA_AR5, (unsigned long)ar5);
    osmgaW32(base, MGA_AR6, (unsigned long)dyR);
    osmgaW32(base, MGA_SGN,
             ((unsigned long)sdxl << 1) | ((unsigned long)sdxr << 5));
    osmgaW32(base, MGA_FXBNDRY,
             (((unsigned long)(right + 1)) << 16) |
             ((unsigned long)left & 0xffffUL));
    osmgaW32(base, MGA_YDSTLEN + MGA_EXEC, (y << 16) | h);
    osmgaW32(base, MGA_DWGCTL, dwgctl);
}

/*
 * D3-2 -- can the engine draw a sloped edge, and does the clip still hold
 * when it does?
 *
 * Three bands in one boot, ordered so a failure is attributable:
 *
 *   rows  0-19   sloped mode, both edges vertical  -> must be a rectangle
 *   rows 20-39   flat colour right triangle
 *   rows 40-59   Gouraud right triangle
 *   rows 60-63   never drawn -- vertical guard
 *
 * The control band is the point of the ordering.  If it does not come out
 * a full-width rectangle then clearing ARZERO/SGNZERO has broken drawing
 * by itself, and nothing about the triangles would be interpretable.
 *
 * x is inset by 8 with guard columns either side, because a sloped edge
 * can walk in the negative direction and nothing so far has ever given it
 * room to be caught doing so.
 */
- (void)runRasterInterpolationTest
{
    IODisplayInfo *di = [self displayInfo];
    const OSMGAFormat *f = &osmgaFmt[selectedFormatIndex];
    vm_address_t base = mmioBase;
    unsigned long stride, testY, byteStart, byteEnd;
    unsigned long x0 = OSMGA_D3_INSET;
    unsigned long x1 = OSMGA_D3_INSET + OSMGA_D3_WIDTH - 1UL;
    vm_address_t alias = 0;
    unsigned long mapLen = 0;
    volatile unsigned long *blk = 0;
    unsigned long row, col, band, guard = 0UL;
    unsigned long firstX[3][5], lastX[3][5], count[3][5];
    unsigned long stair[OSMGA_D3_BAND];
    static const unsigned long probeRow[5] = { 0UL, 5UL, 10UL, 15UL, 19UL };
    IOReturn r;

    if (!rasterTestEnabled || !linearModeActive || !mmioMapped)
        return;
    if (f->bytesPerPixel != 4) {
        IOLog("OpenStepMGA D3-2: not a 32bpp mode, skipped\n");
        return;
    }

    stride    = (unsigned long)di->rowBytes / 4UL;
    testY     = (unsigned long)di->height + OSMGA_S1_GUARD_ROWS;
    byteStart = testY * stride * 4UL;
    byteEnd   = byteStart + (OSMGA_S1_H - 1UL) * stride * 4UL +
                OSMGA_S1_W * 4UL;

    if (byteEnd > OSMGA_S1_VRAM_PROVEN) {
        IOLog("OpenStepMGA D3-2: block past the proven VRAM bound, "
              "skipped\n");
        return;
    }

    r = osmgaMapUncachedBlock(frameBufferPhysical, byteStart, byteEnd,
                              &alias, &mapLen, &blk);
    if (r != IO_R_SUCCESS) {
        IOLog("OpenStepMGA D3-2: uncached alias map failed r=%d\n", (int)r);
        return;
    }
    if (!osmgaStormWaitIdle(base)) {
        IOLog("OpenStepMGA D3-2: engine BUSY at entry, aborted\n");
        goto unmap;
    }

    for (row = 0UL; row < OSMGA_S1_H; row++)
        for (col = 0UL; col < OSMGA_S1_W; col++)
            blk[row * stride + col] = OSMGA_S1_SENTINEL;

    for (band = 0UL; band < 3UL; band++) {
        unsigned long y0 = testY + band * OSMGA_D3_BAND;
        long          rightStart = (band == 0UL) ? (long)x1 : (long)x0;
        long          dxR        = (band == 0UL) ? 0L : (long)(OSMGA_D3_WIDTH - 1UL);

        if (!osmgaStormWaitFifo(base, 13U)) {
            IOLog("OpenStepMGA D3-2: fifo timeout before band %lu\n", band);
            goto unmap;
        }
        osmgaStormInitState(base, stride, x0, x1,
                            y0 * stride, (y0 + OSMGA_D3_BAND - 1UL) * stride);

        if (!osmgaStormWaitFifo(base, 12U)) {
            IOLog("OpenStepMGA D3-2: fifo timeout in band %lu\n", band);
            goto unmap;
        }
        if (band == 2UL) {
            osmgaW32(base, MGA_DR4,  0UL);
            osmgaW32(base, MGA_DR6,  (255UL << 15) / OSMGA_D3_WIDTH);
            osmgaW32(base, MGA_DR7,  0UL);
            osmgaW32(base, MGA_DR8,  0UL);
            osmgaW32(base, MGA_DR10, 0UL);
            osmgaW32(base, MGA_DR11, (255UL << 15) / OSMGA_D3_BAND);
            osmgaW32(base, MGA_DR12, 64UL << 15);
            osmgaW32(base, MGA_DR14, 0UL);
            osmgaW32(base, MGA_DR15, 0UL);
        } else {
            osmgaW32(base, MGA_FCOL, 0x00FF8040UL);
        }

        if (!osmgaStormWaitFifo(base, 11U)) {
            IOLog("OpenStepMGA D3-2: fifo timeout before band %lu draw\n",
                  band);
            goto unmap;
        }
        osmgaStormTrap(base,
                       band == 2UL ? MGA_DWGCTL_GOURAUD
                                   : MGA_DWGCTL_SOLID_FILL,
                       y0, OSMGA_D3_BAND,
                       (long)x0, 0L, (long)OSMGA_D3_BAND, 0L,
                       rightStart, dxR, (long)OSMGA_D3_BAND, 0L);

        if (!osmgaStormWaitIdle(base)) {
            IOLog("OpenStepMGA D3-2: band %lu did not finish -- FAIL\n",
                  band);
            goto unmap;
        }

        for (row = 0UL; row < 5UL; row++) {
            unsigned long ry = band * OSMGA_D3_BAND + probeRow[row];
            unsigned long fi = 0xFFFFUL, la = 0xFFFFUL, n = 0UL;

            for (col = 0UL; col < OSMGA_S1_W; col++)
                if (blk[ry * stride + col] != OSMGA_S1_SENTINEL) {
                    if (fi == 0xFFFFUL) fi = col;
                    la = col; n++;
                }
            firstX[band][row] = fi; lastX[band][row] = la; count[band][row] = n;
        }
    }

    for (band = 0UL; band < 3UL; band++)
        IOLog("OpenStepMGA D3-2: band%lu rows 0/5/10/15/19 first..last(n): "
              "%lu..%lu(%lu) %lu..%lu(%lu) %lu..%lu(%lu) %lu..%lu(%lu) "
              "%lu..%lu(%lu)\n", band,
              firstX[band][0], lastX[band][0], count[band][0],
              firstX[band][1], lastX[band][1], count[band][1],
              firstX[band][2], lastX[band][2], count[band][2],
              firstX[band][3], lastX[band][3], count[band][3],
              firstX[band][4], lastX[band][4], count[band][4]);

    /*
     * Every row of the flat triangle, not just five of them.  The five
     * probe rows leave the error term bounded only to 1..5 -- the whole
     * staircase pins it, and costs one log line.
     */
    for (row = 0UL; row < OSMGA_D3_BAND; row++) {
        unsigned long ry = OSMGA_D3_BAND + row;   /* band 1 */
        unsigned long la = 0UL;

        for (col = 0UL; col < OSMGA_S1_W; col++)
            if (blk[ry * stride + col] != OSMGA_S1_SENTINEL)
                la = col;
        stair[row] = la;
    }
    IOLog("OpenStepMGA D3-2: band1 last-x per row: %lu %lu %lu %lu %lu %lu "
          "%lu %lu %lu %lu\n", stair[0], stair[1], stair[2], stair[3],
          stair[4], stair[5], stair[6], stair[7], stair[8], stair[9]);
    IOLog("OpenStepMGA D3-2: band1 last-x rows 10-19: %lu %lu %lu %lu %lu "
          "%lu %lu %lu %lu %lu\n", stair[10], stair[11], stair[12],
          stair[13], stair[14], stair[15], stair[16], stair[17], stair[18],
          stair[19]);

    /* Guard columns and the four undrawn rows: the only evidence that the
     * clip holds for a sloped span. */
    for (row = 0UL; row < OSMGA_S1_H; row++)
        for (col = 0UL; col < OSMGA_S1_W; col++) {
            int inBand  = row < 3UL * OSMGA_D3_BAND;
            int inCols  = col >= x0 && col <= x1;

            if ((!inBand || !inCols) &&
                blk[row * stride + col] != OSMGA_S1_SENTINEL)
                guard++;
        }
    IOLog("OpenStepMGA D3-2: guard columns/rows disturbed in %lu px\n",
          guard);

    if (count[0][0] == OSMGA_D3_WIDTH && count[0][4] == OSMGA_D3_WIDTH &&
        guard == 0UL)
        IOLog("OpenStepMGA D3-2: control band is a full-width rectangle and "
              "the clip held -- the triangle bands above are meaningful\n");
    else
        IOLog("OpenStepMGA D3-2: control band or clip FAILED -- do not read "
              "the triangle bands\n");

unmap:
    if (alias != 0)
        (void)IOUnmapPhysicalFromIOTask(alias, mapLen);
}


/*
 * D3-3a-0 -- see the note by OSMGA_D3_DSTORG_TEST.
 */
- (void)runDstorgOriginTest
{
    IODisplayInfo *di = [self displayInfo];
    const OSMGAFormat *f = &osmgaFmt[selectedFormatIndex];
    vm_address_t base = mmioBase;
    unsigned long stride = (unsigned long)di->rowBytes / 4UL;
    vm_address_t aBlk = 0, aVis = 0;
    unsigned long lBlk = 0, lVis = 0;
    volatile unsigned long *blk = 0, *vis = 0;
    unsigned long row, col, visBefore = 0UL, visAfter = 0UL, filled = 0UL;
    IOReturn r;

    if (!rasterTestEnabled || !linearModeActive || !mmioMapped)
        return;
    if (f->bytesPerPixel != 4)
        return;

    r = osmgaMapUncachedBlock(frameBufferPhysical, OSMGA_D3_DSTORG_TEST,
                              OSMGA_D3_DSTORG_TEST +
                              OSMGA_D3_DSTORG_ROWS * stride * 4UL,
                              &aBlk, &lBlk, &blk);
    if (r != IO_R_SUCCESS) {
        IOLog("OpenStepMGA D3-3a-0: block alias failed r=%d\n", (int)r);
        return;
    }
    r = osmgaMapUncachedBlock(frameBufferPhysical, 0UL,
                              OSMGA_D3_DSTORG_ROWS * stride * 4UL,
                              &aVis, &lVis, &vis);
    if (r != IO_R_SUCCESS) {
        IOLog("OpenStepMGA D3-3a-0: visible alias failed r=%d\n", (int)r);
        goto unmap;
    }
    if (!osmgaStormWaitIdle(base)) {
        IOLog("OpenStepMGA D3-3a-0: engine BUSY at entry\n");
        goto unmap;
    }

    /* The visible rows are not ours to disturb, so they are only summed --
     * before and after -- never written. */
    for (row = 0UL; row < OSMGA_D3_DSTORG_ROWS; row++)
        for (col = 0UL; col < OSMGA_S1_W; col++)
            visBefore += vis[row * stride + col];

    for (row = 0UL; row < OSMGA_D3_DSTORG_ROWS; row++)
        for (col = 0UL; col < OSMGA_S1_W; col++)
            blk[row * stride + col] = OSMGA_S1_SENTINEL;

    if (!osmgaStormWaitFifo(base, 13U)) {
        IOLog("OpenStepMGA D3-3a-0: fifo timeout before init\n");
        goto unmap;
    }
    osmgaStormInitState(base, stride, 0UL, OSMGA_S1_W - 1UL,
                        0UL, (OSMGA_D3_DSTORG_ROWS - 1UL) * stride);

    if (!osmgaStormWaitFifo(base, 5U)) {
        IOLog("OpenStepMGA D3-3a-0: fifo timeout before draw\n");
        goto unmap;
    }
    /* After initState, which leaves DSTORG at zero. */
    osmgaW32(base, MGA_DSTORG, OSMGA_D3_DSTORG_TEST);
    osmgaW32(base, MGA_FCOL,   OSMGA_S1_FILL);
    osmgaW32(base, MGA_DWGCTL, MGA_DWGCTL_SOLID_FILL);
    osmgaW32(base, MGA_FXBNDRY, (OSMGA_S1_W << 16) | 0UL);
    osmgaW32(base, MGA_YDSTLEN + MGA_EXEC, (0UL << 16) | OSMGA_D3_DSTORG_ROWS);

    if (!osmgaStormWaitIdle(base)) {
        IOLog("OpenStepMGA D3-3a-0: draw did not finish -- FAIL\n");
        osmgaW32(base, MGA_DSTORG, 0UL);
        goto unmap;
    }
    osmgaW32(base, MGA_DSTORG, 0UL);            /* restore, always */

    for (row = 0UL; row < OSMGA_D3_DSTORG_ROWS; row++)
        for (col = 0UL; col < OSMGA_S1_W; col++) {
            if (blk[row * stride + col] == OSMGA_S1_FILL) filled++;
            visAfter += vis[row * stride + col];
        }

    IOLog("OpenStepMGA D3-3a-0: DSTORG=%08lx, block filled %lu/%lu px, "
          "visible sum %08lx -> %08lx\n",
          OSMGA_D3_DSTORG_TEST, filled,
          OSMGA_D3_DSTORG_ROWS * OSMGA_S1_W, visBefore, visAfter);

    if (filled == OSMGA_D3_DSTORG_ROWS * OSMGA_S1_W && visAfter == visBefore)
        IOLog("OpenStepMGA D3-3a-0: PASS -- DSTORG is a byte offset and the "
              "visible area was untouched; depth can use small y\n");
    else if (visAfter != visBefore)
        IOLog("OpenStepMGA D3-3a-0: FAIL -- the visible area changed; "
              "DSTORG did not redirect the write as assumed\n");
    else
        IOLog("OpenStepMGA D3-3a-0: FAIL -- nothing landed in the block "
              "either; the write went somewhere unknown\n");

unmap:
    if (aVis != 0) (void)IOUnmapPhysicalFromIOTask(aVis, lVis);
    if (aBlk != 0) (void)IOUnmapPhysicalFromIOTask(aBlk, lBlk);
}


/*
 * D3-3a -- see the note by OSMGA_D3_ZORG.
 */
- (void)runDepthCompareTest
{
    IODisplayInfo *di = [self displayInfo];
    const OSMGAFormat *f = &osmgaFmt[selectedFormatIndex];
    vm_address_t base = mmioBase;
    unsigned long stride = (unsigned long)di->rowBytes / 4UL;
    unsigned long rows = 3UL * OSMGA_D3_BAND;
    vm_address_t aBlk = 0, aZ = 0;
    unsigned long lBlk = 0, lZ = 0;
    volatile unsigned long *blk = 0, *zb = 0;
    unsigned long band, row, col;
    unsigned long drawn[3], zChanged = 0UL;
    IOReturn r;

    if (!rasterTestEnabled || !linearModeActive || !mmioMapped)
        return;
    if (f->bytesPerPixel != 4)
        return;
    if (OSMGA_D3_ZORG + rows * stride * 4UL > OSMGA_S1_VRAM_PROVEN) {
        IOLog("OpenStepMGA D3-3a: Z area past the proven VRAM bound, "
              "skipped\n");
        return;
    }

    r = osmgaMapUncachedBlock(frameBufferPhysical, OSMGA_S1_VRAM_BLOCK,
                              OSMGA_S1_VRAM_BLOCK + rows * stride * 4UL,
                              &aBlk, &lBlk, &blk);
    if (r != IO_R_SUCCESS) goto unmap;
    r = osmgaMapUncachedBlock(frameBufferPhysical, OSMGA_D3_ZORG,
                              OSMGA_D3_ZORG + rows * stride * 4UL,
                              &aZ, &lZ, &zb);
    if (r != IO_R_SUCCESS) goto unmap;
    if (!osmgaStormWaitIdle(base)) {
        IOLog("OpenStepMGA D3-3a: engine BUSY at entry\n");
        goto unmap;
    }

    for (row = 0UL; row < rows; row++)
        for (col = 0UL; col < OSMGA_S1_W; col++) {
            blk[row * stride + col] = OSMGA_S1_SENTINEL;
            zb[row * stride + col]  = OSMGA_D3_ZCLEAR;
        }

    for (band = 0UL; band < 3UL; band++) {
        unsigned long y0 = band * OSMGA_D3_BAND;
        unsigned long cmp = (band == 0UL) ? MGA_DWGCTL_NOZCMP
                                          : MGA_DWGCTL_ZLT;
        unsigned long z   = (band == 2UL) ? 0xFFFFFFFFUL : 0UL;

        if (!osmgaStormWaitFifo(base, 13U)) goto unmap;
        osmgaStormInitState(base, stride, 0UL, OSMGA_S1_W - 1UL,
                            y0 * stride, (y0 + OSMGA_D3_BAND - 1UL) * stride);

        if (!osmgaStormWaitFifo(base, 10U)) goto unmap;
        osmgaW32(base, MGA_DSTORG, OSMGA_S1_VRAM_BLOCK);
        osmgaW32(base, MGA_ZORG,   OSMGA_D3_ZORG);
        osmgaW32(base, MGA_DR0,    z);
        osmgaW32(base, MGA_DR2,    0UL);
        osmgaW32(base, MGA_DR3,    0UL);
        osmgaW32(base, MGA_FCOL,   OSMGA_S1_FILL);
        osmgaW32(base, MGA_DWGCTL,
                 (MGA_DWGCTL_SOLID_FILL & ~MGA_DWGCTL_ATYPE_I) |
                 MGA_DWGCTL_ATYPE_ZI | cmp);
        osmgaW32(base, MGA_FXBNDRY, (OSMGA_S1_W << 16) | 0UL);
        osmgaW32(base, MGA_YDSTLEN + MGA_EXEC, (y0 << 16) | OSMGA_D3_BAND);

        if (!osmgaStormWaitIdle(base)) {
            IOLog("OpenStepMGA D3-3a: band %lu did not finish\n", band);
            osmgaW32(base, MGA_DSTORG, 0UL);
            osmgaW32(base, MGA_ZORG,   0UL);
            goto unmap;
        }
        osmgaW32(base, MGA_DSTORG, 0UL);
        osmgaW32(base, MGA_ZORG,   0UL);

        drawn[band] = 0UL;
        for (row = y0; row < y0 + OSMGA_D3_BAND; row++)
            for (col = 0UL; col < OSMGA_S1_W; col++)
                if (blk[row * stride + col] == OSMGA_S1_FILL) drawn[band]++;
    }

    for (row = 0UL; row < rows; row++)
        for (col = 0UL; col < OSMGA_S1_W; col++)
            if (zb[row * stride + col] != OSMGA_D3_ZCLEAR) zChanged++;

    IOLog("OpenStepMGA D3-3a: drawn px NOZCMP=%lu ZLT(z=0)=%lu "
          "ZLT(z=max)=%lu of %lu each; Z words changed %lu\n",
          drawn[0], drawn[1], drawn[2], OSMGA_D3_BAND * OSMGA_S1_W, zChanged);

    if (drawn[0] == 0UL)
        IOLog("OpenStepMGA D3-3a: FAIL -- the NOZCMP control drew nothing, "
              "so Z setup broke drawing itself; do not read the other "
              "bands\n");
    else if (drawn[1] > 0UL && drawn[2] == 0UL)
        IOLog("OpenStepMGA D3-3a: PASS -- the Z compare rejects and accepts "
              "as expected\n");
    else if (drawn[1] == drawn[2])
        IOLog("OpenStepMGA D3-3a: FAIL -- both ZLT bands behaved alike, so "
              "the compare is not engaged\n");
    else
        IOLog("OpenStepMGA D3-3a: unexpected -- read the counts above\n");

unmap:
    if (aZ != 0)   (void)IOUnmapPhysicalFromIOTask(aZ, lZ);
    if (aBlk != 0) (void)IOUnmapPhysicalFromIOTask(aBlk, lBlk);
}


/*
 * D3-3a-1 -- see the note by OSMGA_D3_ISO_BANDS.
 */
- (void)runDepthIsolationTest
{
    IODisplayInfo *di = [self displayInfo];
    const OSMGAFormat *f = &osmgaFmt[selectedFormatIndex];
    vm_address_t base = mmioBase;
    unsigned long stride = (unsigned long)di->rowBytes / 4UL;
    unsigned long drawRows = OSMGA_D3_ISO_BANDS * OSMGA_D3_BAND;
    unsigned long allRows = drawRows + OSMGA_D3_ISO_GUARDROWS;
    unsigned long guardW = 2UL * OSMGA_S1_W;
    vm_address_t aBlk = 0, aZ = 0;
    unsigned long lBlk = 0, lZ = 0;
    volatile unsigned long *blk = 0, *zw = 0;
    volatile unsigned short *z16;
    unsigned long band, row, col;
    unsigned long drew[OSMGA_D3_ISO_BANDS], zput[OSMGA_D3_ISO_BANDS];
    unsigned long cGuard = 0UL, zGuard = 0UL;
    unsigned long firstZ = 0xFFFFFFFFUL;
    IOReturn r;

    if (!rasterTestEnabled || !linearModeActive || !mmioMapped)
        return;
    if (f->bytesPerPixel != 4)
        return;
    if (OSMGA_S1_VRAM_BLOCK + allRows * stride * 4UL > OSMGA_D3_ZORG ||
        OSMGA_D3_ZORG + allRows * stride * 2UL > OSMGA_S1_VRAM_PROVEN) {
        IOLog("OpenStepMGA D3-3a-1: windows do not fit the proven VRAM "
              "bound, skipped\n");
        return;
    }

    r = osmgaMapUncachedBlock(frameBufferPhysical, OSMGA_S1_VRAM_BLOCK,
                              OSMGA_S1_VRAM_BLOCK + allRows * stride * 4UL,
                              &aBlk, &lBlk, &blk);
    if (r != IO_R_SUCCESS) goto unmap;
    r = osmgaMapUncachedBlock(frameBufferPhysical, OSMGA_D3_ZORG,
                              OSMGA_D3_ZORG + allRows * stride * 2UL,
                              &aZ, &lZ, &zw);
    if (r != IO_R_SUCCESS) goto unmap;
    z16 = (volatile unsigned short *)zw;
    if (!osmgaStormWaitIdle(base)) {
        IOLog("OpenStepMGA D3-3a-1: engine BUSY at entry\n");
        goto unmap;
    }

    /* Sentinel over the drawn area and the guard margin alike, so a write
     * that escapes either edge shows up as a change. */
    for (row = 0UL; row < allRows; row++)
        for (col = 0UL; col < guardW; col++) {
            blk[row * stride + col] = OSMGA_S1_SENTINEL;
            z16[row * stride + col] = OSMGA_D3_ZSENTINEL;
        }

    for (band = 0UL; band < OSMGA_D3_ISO_BANDS; band++) {
        unsigned long y0 = band * OSMGA_D3_BAND;
        unsigned long zi = (MGA_DWGCTL_SOLID_FILL & ~MGA_DWGCTL_ATYPE_I) |
                           MGA_DWGCTL_ATYPE_ZI;
        unsigned long dwgctl;

        switch (band) {
        case 0: case 1: dwgctl = MGA_DWGCTL_SOLID_FILL;      break;
        case 2:         dwgctl = zi;                         break;
        default:        dwgctl = zi & ~MGA_DWGCTL_SOLID;     break;
        }

        if (!osmgaStormWaitFifo(base, 13U)) goto unmap;
        osmgaStormInitState(base, stride, 0UL, OSMGA_S1_W - 1UL,
                            y0 * stride, (y0 + OSMGA_D3_BAND - 1UL) * stride);

        if (!osmgaStormWaitFifo(base, 16U)) goto unmap;
        osmgaW32(base, MGA_DSTORG, OSMGA_S1_VRAM_BLOCK);
        osmgaW32(base, MGA_ZORG,   OSMGA_D3_ZORG);
        if (band >= 1UL) {
            osmgaW32(base, MGA_DR0, 0UL);
            osmgaW32(base, MGA_DR2, 0UL);
            osmgaW32(base, MGA_DR3, 0UL);
        }
        if (band == 4UL) {
            osmgaW32(base, MGA_DR4,  0UL);
            osmgaW32(base, MGA_DR6,  0UL);
            osmgaW32(base, MGA_DR7,  0UL);
            osmgaW32(base, MGA_DR8,  0UL);
            osmgaW32(base, MGA_DR10, 0UL);
            osmgaW32(base, MGA_DR11, 0UL);
            osmgaW32(base, MGA_DR12, 64UL << 15);
            osmgaW32(base, MGA_DR14, 0UL);
            osmgaW32(base, MGA_DR15, 0UL);
        }

        if (!osmgaStormWaitFifo(base, 5U)) goto unmap;
        osmgaW32(base, MGA_FCOL,    OSMGA_S1_FILL);
        osmgaW32(base, MGA_DWGCTL,  dwgctl);
        osmgaW32(base, MGA_FXBNDRY, (OSMGA_S1_W << 16) | 0UL);
        osmgaW32(base, MGA_YDSTLEN + MGA_EXEC, (y0 << 16) | OSMGA_D3_BAND);

        if (!osmgaStormWaitIdle(base)) {
            IOLog("OpenStepMGA D3-3a-1: band %lu did not finish\n", band);
            osmgaW32(base, MGA_DSTORG, 0UL);
            osmgaW32(base, MGA_ZORG,   0UL);
            goto unmap;
        }
        osmgaW32(base, MGA_DSTORG, 0UL);
        osmgaW32(base, MGA_ZORG,   0UL);

        /* Count what changed, not what matches a colour: bands D and E
         * take their colour from the interpolators, not FCOL. */
        drew[band] = 0UL;
        zput[band] = 0UL;
        for (row = y0; row < y0 + OSMGA_D3_BAND; row++)
            for (col = 0UL; col < OSMGA_S1_W; col++) {
                if (blk[row * stride + col] != OSMGA_S1_SENTINEL)
                    drew[band]++;
                if (z16[row * stride + col] != OSMGA_D3_ZSENTINEL) {
                    if (firstZ == 0xFFFFFFFFUL)
                        firstZ = (unsigned long)z16[row * stride + col];
                    zput[band]++;
                }
            }
    }

    for (row = 0UL; row < allRows; row++)
        for (col = 0UL; col < guardW; col++) {
            if (row < drawRows && col < OSMGA_S1_W)
                continue;                       /* the drawn area itself */
            if (blk[row * stride + col] != OSMGA_S1_SENTINEL) cGuard++;
            if (z16[row * stride + col] != OSMGA_D3_ZSENTINEL) zGuard++;
        }

    IOLog("OpenStepMGA D3-3a-1: drew A=%lu B=%lu C=%lu D=%lu E=%lu of %lu\n",
          drew[0], drew[1], drew[2], drew[3], drew[4],
          OSMGA_D3_BAND * OSMGA_S1_W);
    IOLog("OpenStepMGA D3-3a-1: Z written A=%lu B=%lu C=%lu D=%lu E=%lu; "
          "first Z value %lx\n",
          zput[0], zput[1], zput[2], zput[3], zput[4], firstZ);
    IOLog("OpenStepMGA D3-3a-1: guards disturbed -- colour %lu, depth %lu\n",
          cGuard, zGuard);

    if (zGuard != 0UL)
        IOLog("OpenStepMGA D3-3a-1: STOP -- depth escaped the clip "
              "rectangle; containment for depth cannot rely on clipping\n");
    else if (cGuard != 0UL)
        IOLog("OpenStepMGA D3-3a-1: STOP -- colour escaped the guard\n");
    else if (drew[0] == 0UL)
        IOLog("OpenStepMGA D3-3a-1: FAIL -- the RPL baseline drew nothing, "
              "so this is not about depth; do not read the other bands\n");
    else if (drew[1] == 0UL)
        IOLog("OpenStepMGA D3-3a-1: writing DR0/DR2/DR3 alone stops the "
              "draw -- the cause is those registers, not atype\n");
    else if (drew[3] > 0UL)
        IOLog("OpenStepMGA D3-3a-1: PASS -- atype ZI draws once SOLID is "
              "cleared; the colour interpolators are not required for it\n");
    else if (drew[4] > 0UL)
        IOLog("OpenStepMGA D3-3a-1: PASS -- atype ZI needs SOLID cleared "
              "AND the colour interpolators loaded, exactly like atype I\n");
    else
        IOLog("OpenStepMGA D3-3a-1: FAIL -- atype ZI drew nothing in any "
              "configuration; the hypothesis is wrong and ZI wants "
              "something further\n");

unmap:
    if (aZ != 0)   (void)IOUnmapPhysicalFromIOTask(aZ, lZ);
    if (aBlk != 0) (void)IOUnmapPhysicalFromIOTask(aBlk, lBlk);
}


/*
 * D3-3b -- see the note by OSMGA_D3_ISO_BANDS.
 */
- (void)runDepthEncodingTest
{
    IODisplayInfo *di = [self displayInfo];
    const OSMGAFormat *f = &osmgaFmt[selectedFormatIndex];
    vm_address_t base = mmioBase;
    unsigned long stride = (unsigned long)di->rowBytes / 4UL;
    unsigned long walkRows = 32UL * OSMGA_D3_WALK_ROWS;      /* 64 */
    unsigned long cmpRows = 4UL * OSMGA_D3_CMP_ROWS;         /* 32 */
    unsigned long drawRows = walkRows + cmpRows;             /* 88 */
    unsigned long allRows = drawRows + OSMGA_D3_ISO_GUARDROWS;
    unsigned long guardW = 2UL * OSMGA_S1_W;
    vm_address_t aBlk = 0, aZ = 0;
    unsigned long lBlk = 0, lZ = 0;
    volatile unsigned long *blk = 0, *zw = 0;
    volatile unsigned short *z16;
    unsigned long bit, band, row, col;
    unsigned long zseen[32], drew[4], zcmp[4], colour = 0UL;
    char map[33];
    unsigned long cGuard = 0UL, zGuard = 0UL;
    unsigned long ziBase = (MGA_DWGCTL_SOLID_FILL & ~MGA_DWGCTL_ATYPE_I) |
                           MGA_DWGCTL_ATYPE_ZI;
    IOReturn r;

    if (!rasterTestEnabled || !linearModeActive || !mmioMapped)
        return;
    if (f->bytesPerPixel != 4)
        return;
    if (OSMGA_S1_VRAM_BLOCK + allRows * stride * 4UL > OSMGA_D3_ZORG ||
        OSMGA_D3_ZORG + allRows * stride * 2UL > OSMGA_S1_VRAM_PROVEN) {
        IOLog("OpenStepMGA D3-3b: windows do not fit the proven VRAM "
              "bound, skipped\n");
        return;
    }

    r = osmgaMapUncachedBlock(frameBufferPhysical, OSMGA_S1_VRAM_BLOCK,
                              OSMGA_S1_VRAM_BLOCK + allRows * stride * 4UL,
                              &aBlk, &lBlk, &blk);
    if (r != IO_R_SUCCESS) goto unmap;
    r = osmgaMapUncachedBlock(frameBufferPhysical, OSMGA_D3_ZORG,
                              OSMGA_D3_ZORG + allRows * stride * 2UL,
                              &aZ, &lZ, &zw);
    if (r != IO_R_SUCCESS) goto unmap;
    z16 = (volatile unsigned short *)zw;
    if (!osmgaStormWaitIdle(base)) {
        IOLog("OpenStepMGA D3-3b: engine BUSY at entry\n");
        goto unmap;
    }

    /* Depth addressed as 16-bit elements PITCH apart -- the geometry
     * D3-3a got wrong. */
    for (row = 0UL; row < allRows; row++)
        for (col = 0UL; col < guardW; col++) {
            blk[row * stride + col] = OSMGA_S1_SENTINEL;
            z16[row * stride + col] = OSMGA_D3_ZSENTINEL;
        }

    /* Part 1 -- one band per DR0 bit, NOZCMP so nothing is rejected. */
    for (bit = 0UL; bit < 32UL; bit++) {
        unsigned long y0 = bit * OSMGA_D3_WALK_ROWS;

        if (!osmgaStormWaitFifo(base, 13U)) goto unmap;
        osmgaStormInitState(base, stride, 0UL, OSMGA_S1_W - 1UL,
                            y0 * stride,
                            (y0 + OSMGA_D3_WALK_ROWS - 1UL) * stride);
        if (!osmgaStormWaitFifo(base, 9U)) goto unmap;
        osmgaW32(base, MGA_DSTORG, OSMGA_S1_VRAM_BLOCK);
        osmgaW32(base, MGA_ZORG,   OSMGA_D3_ZORG);
        osmgaW32(base, MGA_DR0,    1UL << bit);
        osmgaW32(base, MGA_DR2,    0UL);
        osmgaW32(base, MGA_DR3,    0UL);
        osmgaW32(base, MGA_DWGCTL, ziBase);
        osmgaW32(base, MGA_FXBNDRY, (OSMGA_S1_W << 16) | 0UL);
        osmgaW32(base, MGA_YDSTLEN + MGA_EXEC,
                 (y0 << 16) | OSMGA_D3_WALK_ROWS);
        if (!osmgaStormWaitIdle(base)) {
            IOLog("OpenStepMGA D3-3b: walk bit %lu did not finish\n", bit);
            osmgaW32(base, MGA_DSTORG, 0UL);
            osmgaW32(base, MGA_ZORG, 0UL);
            goto unmap;
        }
        osmgaW32(base, MGA_DSTORG, 0UL);
        osmgaW32(base, MGA_ZORG,   0UL);
        zseen[bit] = (unsigned long)z16[y0 * stride];
    }

    /* Part 2 -- with depth cleared correctly, does the compare engage? */
    for (band = 0UL; band < 4UL; band++) {
        unsigned long y0 = walkRows + band * OSMGA_D3_CMP_ROWS;
        unsigned long cmp, z;

        switch (band) {
        case 0:  cmp = MGA_DWGCTL_NOZCMP; z = OSMGA_D3_ZMID;  break;
        case 1:  cmp = MGA_DWGCTL_ZLT;    z = OSMGA_D3_ZNEAR; break;
        case 2:  cmp = MGA_DWGCTL_ZLT;    z = OSMGA_D3_ZFAR;  break;
        default: cmp = MGA_DWGCTL_ZGTE;   z = OSMGA_D3_ZFAR;  break;
        }

        if (!osmgaStormWaitFifo(base, 13U)) goto unmap;
        osmgaStormInitState(base, stride, 0UL, OSMGA_S1_W - 1UL,
                            y0 * stride,
                            (y0 + OSMGA_D3_CMP_ROWS - 1UL) * stride);
        if (!osmgaStormWaitFifo(base, 16U)) goto unmap;
        osmgaW32(base, MGA_DSTORG, OSMGA_S1_VRAM_BLOCK);
        osmgaW32(base, MGA_ZORG,   OSMGA_D3_ZORG);
        osmgaW32(base, MGA_DR0,    z);
        osmgaW32(base, MGA_DR2,    0UL);
        osmgaW32(base, MGA_DR3,    0UL);
        osmgaW32(base, MGA_DR4,    0UL);
        osmgaW32(base, MGA_DR6,    0UL);
        osmgaW32(base, MGA_DR7,    0UL);
        osmgaW32(base, MGA_DR8,    0UL);
        osmgaW32(base, MGA_DR10,   0UL);
        osmgaW32(base, MGA_DR11,   0UL);
        osmgaW32(base, MGA_DR12,   OSMGA_D3_CMP_BLUE);
        osmgaW32(base, MGA_DR14,   0UL);
        osmgaW32(base, MGA_DR15,   0UL);
        osmgaW32(base, MGA_DWGCTL, ziBase | cmp);
        osmgaW32(base, MGA_FXBNDRY, (OSMGA_S1_W << 16) | 0UL);
        osmgaW32(base, MGA_YDSTLEN + MGA_EXEC,
                 (y0 << 16) | OSMGA_D3_CMP_ROWS);
        if (!osmgaStormWaitIdle(base)) {
            IOLog("OpenStepMGA D3-3b: compare band %lu did not finish\n",
                  band);
            osmgaW32(base, MGA_DSTORG, 0UL);
            osmgaW32(base, MGA_ZORG, 0UL);
            goto unmap;
        }
        osmgaW32(base, MGA_DSTORG, 0UL);
        osmgaW32(base, MGA_ZORG,   0UL);

        drew[band] = 0UL;
        zcmp[band] = (unsigned long)z16[y0 * stride];
        for (row = y0; row < y0 + OSMGA_D3_CMP_ROWS; row++)
            for (col = 0UL; col < OSMGA_S1_W; col++)
                if (blk[row * stride + col] != OSMGA_S1_SENTINEL) {
                    if (band == 0UL && drew[0] == 0UL)
                        colour = blk[row * stride + col];
                    drew[band]++;
                }
    }

    for (row = 0UL; row < allRows; row++)
        for (col = 0UL; col < guardW; col++) {
            if (row < drawRows && col < OSMGA_S1_W)
                continue;
            if (blk[row * stride + col] != OSMGA_S1_SENTINEL) cGuard++;
            if (z16[row * stride + col] != OSMGA_D3_ZSENTINEL) zGuard++;
        }

    /* One character per DR0 bit, so the whole table survives as a single
     * syslog line: '.' means that bit reached no depth bit, a lowercase
     * hex digit means it reached exactly that depth bit, and an uppercase
     * digit means it reached that bit and others too. */
    for (bit = 0UL; bit < 32UL; bit++) {
        unsigned long v = zseen[bit], j = 0UL;

        if (v == 0UL) { map[bit] = '.'; continue; }
        while ((v & 1UL) == 0UL) { v >>= 1; j++; }
        map[bit] = "0123456789abcdef"[j & 15UL];
        if (v != 1UL && map[bit] >= 'a')
            map[bit] = (char)(map[bit] - 'a' + 'A');
        else if (v != 1UL)
            map[bit] = (char)(map[bit] - '0' + 'G');
    }
    map[32] = '\0';
    IOLog("OpenStepMGA D3-3b: DR0 bit 0..31 -> depth bit  %s\n", map);
    IOLog("OpenStepMGA D3-3b: DR0 bit30=%04lx bit31=%04lx (bit31 is the "
          "one the map cannot express)\n", zseen[30], zseen[31]);
    IOLog("OpenStepMGA D3-3c: drew NOZCMP=%lu ZLT/near=%lu ZLT/far=%lu "
          "ZGTE/far=%lu of %lu\n",
          drew[0], drew[1], drew[2], drew[3],
          OSMGA_D3_CMP_ROWS * OSMGA_S1_W);
    IOLog("OpenStepMGA D3-3c: depth left %04lx/%04lx/%04lx/%04lx "
          "(clear was %04x); control colour %08lx\n",
          zcmp[0], zcmp[1], zcmp[2], zcmp[3], OSMGA_D3_ZSENTINEL, colour);
    IOLog("OpenStepMGA D3-3b: guards disturbed -- colour %lu, depth %lu\n",
          cGuard, zGuard);

    if (cGuard != 0UL || zGuard != 0UL)
        IOLog("OpenStepMGA D3-3b: STOP -- a write escaped the clip\n");
    else if (drew[0] == 0UL)
        IOLog("OpenStepMGA D3-3b: FAIL -- the NOZCMP control drew nothing; "
              "do not read the compare bands\n");
    else if (drew[1] > 0UL && drew[2] == 0UL && drew[3] > 0UL &&
             zcmp[2] == (unsigned long)OSMGA_D3_ZSENTINEL)
        IOLog("OpenStepMGA D3-3c: PASS -- ZLT takes the near depth and "
              "rejects the far one, ZGTE takes the far one, and the "
              "rejected band left the depth buffer untouched\n");
    else if (drew[1] == drew[2] && drew[2] == drew[3])
        IOLog("OpenStepMGA D3-3c: FAIL -- every compare mode behaved "
              "alike, so the comparator is not engaged at all\n");
    else
        IOLog("OpenStepMGA D3-3c: partial -- the modes differ but not as "
              "predicted; read the counts and the depth left above\n");

unmap:
    if (aZ != 0)   (void)IOUnmapPhysicalFromIOTask(aZ, lZ);
    if (aBlk != 0) (void)IOUnmapPhysicalFromIOTask(aBlk, lBlk);
}



/* Program the texture unit for the identity 64x64 texture at OSMGA_D3_TEXORG
 * with the given per-pixel step, shared by every D3-4b band. */
static void
osmgaTextureSetup(vm_address_t base, unsigned long dim, unsigned long log2dim,
                  unsigned long texPitch, unsigned long stepX,
                  unsigned long stepY)
{
    osmgaW32(base, MGA_TEXORG,       OSMGA_D3_TEXORG);
    osmgaW32(base, MGA_TEXWIDTH,     ((dim - 1UL) << 18) |
                                     (((8UL - log2dim) & 63UL) << 9) | log2dim);
    osmgaW32(base, MGA_TEXHEIGHT,    ((dim - 1UL) << 18) |
                                     (((8UL - log2dim) & 63UL) << 9) | log2dim);
    osmgaW32(base, MGA_TEXCTL,       MGA_TEXCTL_PITCHLIN |
                                     ((texPitch & 2047UL) << 9) |
                                     MGA_TEXCTL_NOPERSP | MGA_TEXCTL_TAKEY |
                                     MGA_TEXCTL_CLAMPUV | MGA_TEXCTL_TW32);
    osmgaW32(base, MGA_TEXCTL2,      MGA_TEXCTL2_G400_MAGIC |
                                     MGA_TEXCTL2_CKSTRANSDIS);
    osmgaW32(base, MGA_TEXFILTER,    MGA_TEXFILTER_ALPHA | (0x10UL << 21));
    osmgaW32(base, MGA_TEXTRANS,     0x0000ffffUL);
    osmgaW32(base, MGA_TEXTRANSHIGH, 0x0000ffffUL);
    osmgaW32(base, MGA_TDUALSTAGE0,  0UL);
    osmgaW32(base, MGA_TDUALSTAGE1,  0UL);
    osmgaW32(base, MGA_ALPHACTRL,    MGA_ALPHACTRL_OPAQUE);
    osmgaW32(base, MGA_TMR0,         stepX);
    osmgaW32(base, MGA_TMR0 + 4UL,   0UL);
    osmgaW32(base, MGA_TMR0 + 8UL,   0UL);
    osmgaW32(base, MGA_TMR3,         stepY);
    osmgaW32(base, MGA_TMR0 + 16UL,  0UL);
    osmgaW32(base, MGA_TMR0 + 20UL,  0UL);
    osmgaW32(base, MGA_TMR0 + 24UL,  0UL);   /* TMR6 -- u origin */
    osmgaW32(base, MGA_TMR0 + 28UL,  0UL);   /* TMR7 -- v origin */
    osmgaW32(base, MGA_TMR8,         1UL << 16);
}

/*
 * D3-4a -- see the note by MGA_TMR0.
 */
- (void)runTextureIdentityTest
{
    IODisplayInfo *di = [self displayInfo];
    const OSMGAFormat *f = &osmgaFmt[selectedFormatIndex];
    vm_address_t base = mmioBase;
    unsigned long stride = (unsigned long)di->rowBytes / 4UL;
    unsigned long dim = OSMGA_D3_TEXDIM;
    unsigned long texPitch = dim;
    unsigned long texBytes = dim * texPitch * 4UL;
    unsigned long allRows = dim + OSMGA_D3_ISO_GUARDROWS;
    unsigned long guardW = 2UL * OSMGA_S1_W;
    unsigned long step = 1UL << (20UL - OSMGA_D3_TEXLOG2);   /* one texel */
    vm_address_t aBlk = 0, aTex = 0;
    unsigned long lBlk = 0, lTex = 0;
    volatile unsigned long *blk = 0, *tex = 0;
    unsigned long row, col, ident = 0UL, drawn = 0UL;
    unsigned long cGuard = 0UL, texDirty = 0UL;
    IOReturn r;

    if (!rasterTestEnabled || !linearModeActive || !mmioMapped)
        return;
    if (f->bytesPerPixel != 4)
        return;
    if (OSMGA_D3_TEXORG + texBytes > OSMGA_S1_VRAM_PROVEN ||
        OSMGA_S1_VRAM_BLOCK + allRows * stride * 4UL > OSMGA_D3_ZORG) {
        IOLog("OpenStepMGA D3-4a: windows do not fit the proven VRAM "
              "bound, skipped\n");
        return;
    }

    r = osmgaMapUncachedBlock(frameBufferPhysical, OSMGA_S1_VRAM_BLOCK,
                              OSMGA_S1_VRAM_BLOCK + allRows * stride * 4UL,
                              &aBlk, &lBlk, &blk);
    if (r != IO_R_SUCCESS) goto unmap;
    r = osmgaMapUncachedBlock(frameBufferPhysical, OSMGA_D3_TEXORG,
                              OSMGA_D3_TEXORG + texBytes,
                              &aTex, &lTex, &tex);
    if (r != IO_R_SUCCESS) goto unmap;
    if (!osmgaStormWaitIdle(base)) {
        IOLog("OpenStepMGA D3-4a: engine BUSY at entry\n");
        goto unmap;
    }

    /* Each texel carries its own coordinates, so the readback says which
     * texel arrived rather than only whether something did. */
    for (row = 0UL; row < dim; row++)
        for (col = 0UL; col < dim; col++)
            tex[row * texPitch + col] = (col << 16) | (row << 8) | 0x40UL;
    for (row = 0UL; row < allRows; row++)
        for (col = 0UL; col < guardW; col++)
            blk[row * stride + col] = OSMGA_S1_SENTINEL;

    if (!osmgaStormWaitFifo(base, 13U)) goto unmap;
    osmgaStormInitState(base, stride, 0UL, dim - 1UL, 0UL,
                        (dim - 1UL) * stride);

    if (!osmgaStormWaitFifo(base, 16U)) goto unmap;
    osmgaW32(base, MGA_DSTORG,       OSMGA_S1_VRAM_BLOCK);
    osmgaW32(base, MGA_TEXORG,       OSMGA_D3_TEXORG);
    osmgaW32(base, MGA_TEXWIDTH,     ((dim - 1UL) << 18) |
                                     (((8UL - OSMGA_D3_TEXLOG2) & 63UL) << 9) |
                                     OSMGA_D3_TEXLOG2);
    osmgaW32(base, MGA_TEXHEIGHT,    ((dim - 1UL) << 18) |
                                     (((8UL - OSMGA_D3_TEXLOG2) & 63UL) << 9) |
                                     OSMGA_D3_TEXLOG2);
    osmgaW32(base, MGA_TEXCTL,       MGA_TEXCTL_PITCHLIN |
                                     ((texPitch & 2047UL) << 9) |
                                     MGA_TEXCTL_NOPERSP | MGA_TEXCTL_TAKEY |
                                     MGA_TEXCTL_CLAMPUV | MGA_TEXCTL_TW32);
    osmgaW32(base, MGA_TEXCTL2,      MGA_TEXCTL2_G400_MAGIC |
                                     MGA_TEXCTL2_CKSTRANSDIS);
    osmgaW32(base, MGA_TEXFILTER,    MGA_TEXFILTER_ALPHA | (0x10UL << 21));
    osmgaW32(base, MGA_TEXTRANS,     0x0000ffffUL);
    osmgaW32(base, MGA_TEXTRANSHIGH, 0x0000ffffUL);
    osmgaW32(base, MGA_TDUALSTAGE0,  0UL);
    osmgaW32(base, MGA_TDUALSTAGE1,  0UL);
    osmgaW32(base, MGA_ALPHACTRL,    MGA_ALPHACTRL_OPAQUE);

    if (!osmgaStormWaitFifo(base, 14U)) goto unmap;
    osmgaW32(base, MGA_TMR0, step);          /* one texel per pixel in x */
    osmgaW32(base, MGA_TMR0 + 4UL,  0UL);
    osmgaW32(base, MGA_TMR0 + 8UL,  0UL);
    osmgaW32(base, MGA_TMR3, step);          /* one texel per pixel in y */
    osmgaW32(base, MGA_TMR0 + 16UL, 0UL);
    osmgaW32(base, MGA_TMR0 + 20UL, 0UL);
    osmgaW32(base, MGA_TMR0 + 24UL, 0UL);    /* TMR6 -- u origin */
    osmgaW32(base, MGA_TMR0 + 28UL, 0UL);    /* TMR7 -- v origin */
    osmgaW32(base, MGA_TMR8, 1UL << 16);     /* no decal on the H family */

    osmgaW32(base, MGA_DWGCTL,
             (MGA_DWGCTL_GOURAUD & ~MGA_DWGCTL_OPCODE_MASK) |
             MGA_DWGCTL_TEXTURE_TRAP);
    osmgaW32(base, MGA_FXBNDRY, (dim << 16) | 0UL);
    osmgaW32(base, MGA_YDSTLEN + MGA_EXEC, (0UL << 16) | dim);

    if (!osmgaStormWaitIdle(base)) {
        IOLog("OpenStepMGA D3-4a: the draw did not finish\n");
        osmgaW32(base, MGA_DSTORG, 0UL);
        goto unmap;
    }
    osmgaW32(base, MGA_DSTORG, 0UL);

    for (row = 0UL; row < dim; row++)
        for (col = 0UL; col < dim; col++) {
            unsigned long got = blk[row * stride + col];

            if (got != OSMGA_S1_SENTINEL) drawn++;
            if (got == tex[row * texPitch + col]) ident++;
        }
    for (row = 0UL; row < allRows; row++)
        for (col = 0UL; col < guardW; col++) {
            if (row < dim && col < dim) continue;
            if (blk[row * stride + col] != OSMGA_S1_SENTINEL) cGuard++;
        }
    for (row = 0UL; row < dim; row++)
        for (col = 0UL; col < dim; col++)
            if (tex[row * texPitch + col] !=
                ((col << 16) | (row << 8) | 0x40UL)) texDirty++;

    IOLog("OpenStepMGA D3-4a: drawn %lu, identity %lu of %lu; texel step "
          "%lx; texture dirty %lu, colour guard %lu\n",
          drawn, ident, dim * dim, step, texDirty, cGuard);
    /* Which texel actually arrived, so a constant scale or offset error
     * yields the right value instead of just a failure. */
    IOLog("OpenStepMGA D3-4a: row0 x=0,16,32,48,63 -> %06lx %06lx %06lx "
          "%06lx %06lx\n",
          blk[0] & 0xffffffUL,          blk[16] & 0xffffffUL,
          blk[32] & 0xffffffUL,         blk[48] & 0xffffffUL,
          blk[63] & 0xffffffUL);
    IOLog("OpenStepMGA D3-4a: col0 y=0,16,32,48,63 -> %06lx %06lx %06lx "
          "%06lx %06lx\n",
          blk[0] & 0xffffffUL,          blk[16UL * stride] & 0xffffffUL,
          blk[32UL * stride] & 0xffffffUL, blk[48UL * stride] & 0xffffffUL,
          blk[63UL * stride] & 0xffffffUL);

    if (texDirty != 0UL)
        IOLog("OpenStepMGA D3-4a: STOP -- the texture was written to; "
              "TEXORG and DSTORG must be overlapping\n");
    else if (cGuard != 0UL)
        IOLog("OpenStepMGA D3-4a: STOP -- colour escaped the clip\n");
    else if (ident == dim * dim)
        IOLog("OpenStepMGA D3-4a: PASS -- every pixel received its own "
              "texel; TEXTURE_TRAP and the coordinate scale are both "
              "right\n");
    else if (drawn == 0UL)
        IOLog("OpenStepMGA D3-4a: FAIL -- nothing drew at all\n");
    else
        IOLog("OpenStepMGA D3-4a: drew but the mapping is not the "
              "identity; read the two sample rows above\n");

unmap:
    if (aTex != 0) (void)IOUnmapPhysicalFromIOTask(aTex, lTex);
    if (aBlk != 0) (void)IOUnmapPhysicalFromIOTask(aBlk, lBlk);
}


/*
 * D3-4b -- see the note by OSMGA_D3_4B_BAND.
 */
- (void)runTextureSlopeTest
{
    IODisplayInfo *di = [self displayInfo];
    const OSMGAFormat *f = &osmgaFmt[selectedFormatIndex];
    vm_address_t base = mmioBase;
    unsigned long stride = (unsigned long)di->rowBytes / 4UL;
    unsigned long dim = OSMGA_D3_TEXDIM;
    unsigned long texPitch = dim;
    unsigned long texBytes = dim * texPitch * 4UL;
    unsigned long band = OSMGA_D3_4B_BAND;
    unsigned long drawRows = 4UL * band;                      /* 80 */
    unsigned long allRows = drawRows + OSMGA_D3_ISO_GUARDROWS;
    unsigned long guardW = 2UL * OSMGA_S1_W;
    unsigned long step = 1UL << (20UL - OSMGA_D3_TEXLOG2);
    unsigned long dwgTex = (MGA_DWGCTL_GOURAUD & ~MGA_DWGCTL_OPCODE_MASK) |
                           MGA_DWGCTL_TEXTURE_TRAP;
    vm_address_t aBlk = 0, aTex = 0, aZ = 0;
    unsigned long lBlk = 0, lTex = 0, lZ = 0;
    volatile unsigned long *blk = 0, *tex = 0, *zw = 0;
    volatile unsigned short *z16;
    unsigned long b, row, col;
    unsigned long uA[5], uB[5], firstX[5], uC[5], vB0 = 0xFFFFUL;
    unsigned long drewD = 0UL, zOutside = 0UL;
    unsigned long cGuard = 0UL, zGuard = 0UL, texDirty = 0UL;
    static const unsigned long sampleX[5] = { 0UL, 16UL, 32UL, 48UL, 63UL };
    static const unsigned long sampleRow[5] = { 0UL, 5UL, 10UL, 15UL, 19UL };
    IOReturn r;

    if (!rasterTestEnabled || !linearModeActive || !mmioMapped)
        return;
    if (f->bytesPerPixel != 4)
        return;
    if (OSMGA_D3_TEXORG + texBytes > OSMGA_S1_VRAM_PROVEN ||
        OSMGA_S1_VRAM_BLOCK + allRows * stride * 4UL > OSMGA_D3_ZORG ||
        OSMGA_D3_ZORG + allRows * stride * 2UL > OSMGA_D3_TEXORG) {
        IOLog("OpenStepMGA D3-4b: windows do not fit, skipped\n");
        return;
    }

    r = osmgaMapUncachedBlock(frameBufferPhysical, OSMGA_S1_VRAM_BLOCK,
                              OSMGA_S1_VRAM_BLOCK + allRows * stride * 4UL,
                              &aBlk, &lBlk, &blk);
    if (r == IO_R_SUCCESS)
        r = osmgaMapUncachedBlock(frameBufferPhysical, OSMGA_D3_ZORG,
                                  OSMGA_D3_ZORG + allRows * stride * 2UL,
                                  &aZ, &lZ, &zw);
    if (r == IO_R_SUCCESS)
        r = osmgaMapUncachedBlock(frameBufferPhysical, OSMGA_D3_TEXORG,
                                  OSMGA_D3_TEXORG + texBytes,
                                  &aTex, &lTex, &tex);
    if (r != IO_R_SUCCESS) {
        IOLog("OpenStepMGA D3-4b: could not map the three windows (%d)\n",
              (int)r);
        goto unmap;
    }
    z16 = (volatile unsigned short *)zw;
    if (!osmgaStormWaitIdle(base)) {
        IOLog("OpenStepMGA D3-4b: engine BUSY at entry\n");
        goto unmap;
    }

    for (row = 0UL; row < dim; row++)
        for (col = 0UL; col < dim; col++)
            tex[row * texPitch + col] = (col << 16) | (row << 8) | 0x40UL;
    for (row = 0UL; row < allRows; row++)
        for (col = 0UL; col < guardW; col++) {
            blk[row * stride + col] = OSMGA_S1_SENTINEL;
            z16[row * stride + col] = OSMGA_D3_ZSENTINEL;
        }

    for (b = 0UL; b < 4UL; b++) {
        unsigned long y0 = b * band;
        unsigned long sx = (b == 0UL) ? step * 2UL
                         : (b == 1UL) ? step / 2UL : step;

        if (!osmgaStormWaitFifo(base, 13U)) goto fifo;
        osmgaStormInitState(base, stride, 0UL, dim - 1UL,
                            y0 * stride, (y0 + band - 1UL) * stride);
        if (!osmgaStormWaitFifo(base, 12U)) goto fifo;
        osmgaW32(base, MGA_DSTORG, OSMGA_S1_VRAM_BLOCK);
        osmgaTextureSetup(base, dim, OSMGA_D3_TEXLOG2, texPitch, sx, sx);

        if (b < 2UL) {
            if (!osmgaStormWaitFifo(base, 4U)) goto fifo;
            osmgaW32(base, MGA_DWGCTL,  dwgTex);
            osmgaW32(base, MGA_FXBNDRY, (dim << 16) | 0UL);
            osmgaW32(base, MGA_YDSTLEN + MGA_EXEC, (y0 << 16) | band);
        } else {
            /* Sloped LEFT edge: the row's first pixel moves right with y,
             * which is what makes the texel there discriminating. */
            unsigned long dwg = dwgTex;

            if (b == 3UL) {
                if (!osmgaStormWaitFifo(base, 5U)) goto fifo;
                osmgaW32(base, MGA_ZORG, OSMGA_D3_ZORG);
                osmgaW32(base, MGA_DR0,  OSMGA_D3_ZMID);
                osmgaW32(base, MGA_DR2,  0UL);
                osmgaW32(base, MGA_DR3,  0UL);
                dwg = (dwgTex & ~MGA_DWGCTL_ATYPE_I) | MGA_DWGCTL_ATYPE_ZI;
            }
            if (!osmgaStormWaitFifo(base, 11U)) goto fifo;
            osmgaStormTrap(base, dwg, y0, band,
                           0L, OSMGA_D3_4B_SLOPE, (long)band, 0L,
                           (long)(dim - 1UL), 0L, (long)band, 0L);
        }

        if (!osmgaStormWaitIdle(base)) {
            IOLog("OpenStepMGA D3-4b: band %lu did not finish\n", b);
            osmgaW32(base, MGA_DSTORG, 0UL);
            osmgaW32(base, MGA_ZORG, 0UL);
            goto unmap;
        }
        osmgaW32(base, MGA_DSTORG, 0UL);
        osmgaW32(base, MGA_ZORG,   0UL);
    }

    for (row = 0UL; row < 5UL; row++) {
        uA[row] = (blk[sampleX[row]] >> 16) & 0xffUL;
        uB[row] = (blk[band * stride + sampleX[row]] >> 16) & 0xffUL;
    }
    vB0 = (blk[band * stride] >> 8) & 0xffUL;

    for (row = 0UL; row < 5UL; row++) {
        unsigned long ry = 2UL * band + sampleRow[row];

        firstX[row] = 0xFFFFUL;
        uC[row] = 0xFFFFUL;
        for (col = 0UL; col < dim; col++)
            if (blk[ry * stride + col] != OSMGA_S1_SENTINEL) {
                firstX[row] = col;
                uC[row] = (blk[ry * stride + col] >> 16) & 0xffUL;
                break;
            }
    }

    /* Depth that ignored the edge walk would land inside the band rows but
     * outside the drawn span -- sharper than an outer guard. */
    for (row = 3UL * band; row < 4UL * band; row++)
        for (col = 0UL; col < dim; col++) {
            int inside = (blk[row * stride + col] != OSMGA_S1_SENTINEL);

            if (inside) drewD++;
            if (!inside && z16[row * stride + col] != OSMGA_D3_ZSENTINEL)
                zOutside++;
        }

    for (row = 0UL; row < allRows; row++)
        for (col = 0UL; col < guardW; col++) {
            if (row < drawRows && col < dim) continue;
            if (blk[row * stride + col] != OSMGA_S1_SENTINEL) cGuard++;
            if (z16[row * stride + col] != OSMGA_D3_ZSENTINEL) zGuard++;
        }
    for (row = 0UL; row < dim; row++)
        for (col = 0UL; col < dim; col++)
            if (tex[row * texPitch + col] !=
                ((col << 16) | (row << 8) | 0x40UL)) texDirty++;

    IOLog("OpenStepMGA D3-4b: A x2 u at x=0,16,32,48,63 -> %lu %lu %lu %lu "
          "%lu (expect 0 32 64 96 126 before clamping)\n",
          uA[0], uA[1], uA[2], uA[3], uA[4]);
    IOLog("OpenStepMGA D3-4b: B /2 u -> %lu %lu %lu %lu %lu (expect "
          "0 8 16 24 31); v at its first row %lu (0 = primitive origin, "
          "10 = screen origin)\n",
          uB[0], uB[1], uB[2], uB[3], uB[4], vB0);
    IOLog("OpenStepMGA D3-4b: C sloped rows 0,5,10,15,19 firstx/u -> "
          "%lu/%lu %lu/%lu %lu/%lu %lu/%lu %lu/%lu\n",
          firstX[0], uC[0], firstX[1], uC[1], firstX[2], uC[2],
          firstX[3], uC[3], firstX[4], uC[4]);
    IOLog("OpenStepMGA D3-4b: D drew %lu; depth outside the span %lu; "
          "guards colour %lu depth %lu; texture dirty %lu\n",
          drewD, zOutside, cGuard, zGuard, texDirty);

    if (texDirty != 0UL || cGuard != 0UL || zGuard != 0UL)
        IOLog("OpenStepMGA D3-4b: STOP -- a write escaped its region\n");
    else if (zOutside != 0UL)
        IOLog("OpenStepMGA D3-4b: STOP -- depth was written outside the "
              "sloped span; depth does not follow the edge walk\n");
    else if (firstX[4] > firstX[0] && uC[4] == firstX[4])
        IOLog("OpenStepMGA D3-4b: PASS -- the texel follows the pixel's own "
              "x across a sloped edge, so no per-row reseeding is needed\n");
    else if (firstX[4] > firstX[0] && uC[4] == 0UL)
        IOLog("OpenStepMGA D3-4b: the texture accumulates from each span's "
              "start -- the driver must reseed TMR6 per row\n");
    else
        IOLog("OpenStepMGA D3-4b: read the samples above\n");

    goto unmap;

fifo:
    IOLog("OpenStepMGA D3-4b: fifo wait timed out in band %lu\n", b);
    osmgaW32(base, MGA_DSTORG, 0UL);
    osmgaW32(base, MGA_ZORG,   0UL);

unmap:
    if (aTex != 0) (void)IOUnmapPhysicalFromIOTask(aTex, lTex);
    if (aZ != 0)   (void)IOUnmapPhysicalFromIOTask(aZ, lZ);
    if (aBlk != 0) (void)IOUnmapPhysicalFromIOTask(aBlk, lBlk);
}


/*
 * D3-4c -- see the note by OSMGA_D3_CANARY.
 */
- (void)runTextureOriginTest
{
    IODisplayInfo *di = [self displayInfo];
    const OSMGAFormat *f = &osmgaFmt[selectedFormatIndex];
    vm_address_t base = mmioBase;
    unsigned long stride = (unsigned long)di->rowBytes / 4UL;
    unsigned long dim = OSMGA_D3_TEXDIM;
    unsigned long texPitch = dim;
    unsigned long texBytes = dim * texPitch * 4UL;
    unsigned long band = OSMGA_D3_4B_BAND;
    unsigned long drawRows = 3UL * band;
    unsigned long allRows = drawRows + OSMGA_D3_ISO_GUARDROWS;
    unsigned long step = 1UL << (20UL - OSMGA_D3_TEXLOG2);
    unsigned long dwgTex = (MGA_DWGCTL_GOURAUD & ~MGA_DWGCTL_OPCODE_MASK) |
                           MGA_DWGCTL_TEXTURE_TRAP;
    vm_address_t aBlk = 0, aTex = 0;
    unsigned long lBlk = 0, lTex = 0;
    volatile unsigned long *blk = 0, *tex = 0;
    unsigned long i, row, col;
    unsigned long uE[5], vF[3], alien = 0UL, alienVal = 0UL, drewG = 0UL;
    static const unsigned long sampleRow[5] = { 0UL, 5UL, 10UL, 15UL, 19UL };
    IOReturn r;

    if (!rasterTestEnabled || !linearModeActive || !mmioMapped)
        return;
    if (f->bytesPerPixel != 4)
        return;
    if (OSMGA_D3_TEXORG + texBytes + OSMGA_D3_CANARY_BYTES >
            OSMGA_S1_VRAM_PROVEN) {
        IOLog("OpenStepMGA D3-4c: the canary does not fit the proven VRAM "
              "bound, skipped\n");
        return;
    }

    r = osmgaMapUncachedBlock(frameBufferPhysical, OSMGA_S1_VRAM_BLOCK,
                              OSMGA_S1_VRAM_BLOCK + allRows * stride * 4UL,
                              &aBlk, &lBlk, &blk);
    if (r == IO_R_SUCCESS)
        r = osmgaMapUncachedBlock(frameBufferPhysical, OSMGA_D3_TEXORG,
                                  OSMGA_D3_TEXORG + texBytes +
                                  OSMGA_D3_CANARY_BYTES,
                                  &aTex, &lTex, &tex);
    if (r != IO_R_SUCCESS) {
        IOLog("OpenStepMGA D3-4c: could not map the windows (%d)\n", (int)r);
        goto unmap;
    }
    if (!osmgaStormWaitIdle(base)) {
        IOLog("OpenStepMGA D3-4c: engine BUSY at entry\n");
        goto unmap;
    }

    for (row = 0UL; row < dim; row++)
        for (col = 0UL; col < dim; col++)
            tex[row * texPitch + col] = (col << 16) | (row << 8) | 0x40UL;
    for (i = dim * texPitch; i < dim * texPitch +
             OSMGA_D3_CANARY_BYTES / 4UL; i++)
        tex[i] = OSMGA_D3_CANARY;
    for (row = 0UL; row < allRows; row++)
        for (col = 0UL; col < 2UL * OSMGA_S1_W; col++)
            blk[row * stride + col] = OSMGA_S1_SENTINEL;

    for (i = 0UL; i < 3UL; i++) {
        unsigned long y0 = i * band;
        unsigned long sx = (i == 2UL) ? step * 8UL : step;
        unsigned long clipTop = y0;
        unsigned long drawTop = y0;
        unsigned long drawLen = band;

        /* Band F: clip the full band but draw only its middle, so the two
         * candidate origins no longer coincide. */
        if (i == 1UL) { drawTop = y0 + 5UL; drawLen = band - 10UL; }

        if (!osmgaStormWaitFifo(base, 13U)) goto fifo;
        osmgaStormInitState(base, stride, 0UL, dim - 1UL,
                            clipTop * stride, (y0 + band - 1UL) * stride);
        if (!osmgaStormWaitFifo(base, 12U)) goto fifo;
        osmgaW32(base, MGA_DSTORG, OSMGA_S1_VRAM_BLOCK);
        osmgaTextureSetup(base, dim, OSMGA_D3_TEXLOG2, texPitch, sx, sx);

        if (i == 0UL) {
            if (!osmgaStormWaitFifo(base, 11U)) goto fifo;
            osmgaStormTrap(base, dwgTex, drawTop, drawLen,
                           0L, OSMGA_D3_4B_SLOPE, (long)drawLen, 0L,
                           (long)(dim - 1UL), 0L, (long)drawLen, 0L);
        } else {
            if (!osmgaStormWaitFifo(base, 4U)) goto fifo;
            osmgaW32(base, MGA_DWGCTL,  dwgTex);
            osmgaW32(base, MGA_FXBNDRY, (dim << 16) | 0UL);
            osmgaW32(base, MGA_YDSTLEN + MGA_EXEC, (drawTop << 16) | drawLen);
        }

        if (!osmgaStormWaitIdle(base)) {
            IOLog("OpenStepMGA D3-4c: band %lu did not finish\n", i);
            osmgaW32(base, MGA_DSTORG, 0UL);
            goto unmap;
        }
        osmgaW32(base, MGA_DSTORG, 0UL);
    }

    /* Band E: one fixed column, every sampled row.  x = 40 is inside the
     * span for all of them, since the left edge reaches only 38. */
    for (i = 0UL; i < 5UL; i++) {
        unsigned long v = blk[sampleRow[i] * stride + 40UL];

        uE[i] = (v == OSMGA_S1_SENTINEL) ? 0xFFFFUL : ((v >> 16) & 0xffUL);
    }
    /* Band F: v at the first, middle and last drawn row. */
    vF[0] = (blk[(band + 5UL) * stride] >> 8) & 0xffUL;
    vF[1] = (blk[(band + 10UL) * stride] >> 8) & 0xffUL;
    vF[2] = (blk[(band + 14UL) * stride] >> 8) & 0xffUL;
    /* Band G: any pixel holding something no texel can encode. */
    for (row = 2UL * band; row < 3UL * band; row++)
        for (col = 0UL; col < dim; col++) {
            unsigned long v = blk[row * stride + col];

            if (v == OSMGA_S1_SENTINEL) continue;
            drewG++;
            if ((v & 0xffUL) != 0x40UL || ((v >> 16) & 0xffUL) > 63UL ||
                ((v >> 8) & 0xffUL) > 63UL || (v >> 24) != 0UL) {
                if (alien == 0UL) alienVal = v;
                alien++;
            }
        }

    IOLog("OpenStepMGA D3-4c: E fixed column x=40, rows 0,5,10,15,19 -> "
          "u %lu %lu %lu %lu %lu (one value = u depends on x alone)\n",
          uE[0], uE[1], uE[2], uE[3], uE[4]);
    IOLog("OpenStepMGA D3-4c: F clip top %lu, draw top %lu; v at draw rows "
          "0,5,9 -> %lu %lu %lu (0,5,9 = primitive origin; 5,10,14 = clip "
          "origin)\n", band, band + 5UL, vF[0], vF[1], vF[2]);
    IOLog("OpenStepMGA D3-4c: G magnified 8x drew %lu, pixels holding a "
          "non-texel value %lu (first %08lx, canary is %08lx)\n",
          drewG, alien, alienVal, OSMGA_D3_CANARY);

    if (alien != 0UL)
        IOLog("OpenStepMGA D3-4c: STOP -- the texture unit fetched outside "
              "its allocation; CLAMPUV bounds the coordinate but not the "
              "address\n");
    else if (uE[0] == uE[4] && uE[0] == 40UL)
        IOLog("OpenStepMGA D3-4c: PASS -- u depends only on the pixel's x, "
              "the address stays inside the texture, and the origin is "
              "read from the v samples above\n");
    else
        IOLog("OpenStepMGA D3-4c: read the samples above\n");
    goto unmap;

fifo:
    IOLog("OpenStepMGA D3-4c: fifo wait timed out in band %lu\n", i);
    osmgaW32(base, MGA_DSTORG, 0UL);

unmap:
    if (aTex != 0) (void)IOUnmapPhysicalFromIOTask(aTex, lTex);
    if (aBlk != 0) (void)IOUnmapPhysicalFromIOTask(aBlk, lBlk);
}


/*
 * D3-5a -- see the note by MGA_ALPHASTART.
 */
- (void)runAlphaSourceTest
{
    IODisplayInfo *di = [self displayInfo];
    const OSMGAFormat *f = &osmgaFmt[selectedFormatIndex];
    vm_address_t base = mmioBase;
    unsigned long stride = (unsigned long)di->rowBytes / 4UL;
    unsigned long drawRows = OSMGA_D3_ALPHA_BANDS * OSMGA_D3_ALPHA_ROWS;
    unsigned long allRows = drawRows + OSMGA_D3_ISO_GUARDROWS;
    unsigned long guardW = 2UL * OSMGA_S1_W;
    unsigned long blend = MGA_ALPHA_SRC_ALPHA | MGA_ALPHA_DST_ZERO |
                          MGA_ALPHA_CHANNEL;
    vm_address_t aBlk = 0;
    unsigned long lBlk = 0;
    volatile unsigned long *blk = 0;
    unsigned long b, row, col, seen[OSMGA_D3_ALPHA_BANDS];
    unsigned long cGuard = 0UL;
    char map[33];
    IOReturn r;

    if (!rasterTestEnabled || !linearModeActive || !mmioMapped)
        return;
    if (f->bytesPerPixel != 4)
        return;
    if (OSMGA_S1_VRAM_BLOCK + allRows * stride * 4UL > OSMGA_D3_ZORG) {
        IOLog("OpenStepMGA D3-5a: window does not fit, skipped\n");
        return;
    }

    r = osmgaMapUncachedBlock(frameBufferPhysical, OSMGA_S1_VRAM_BLOCK,
                              OSMGA_S1_VRAM_BLOCK + allRows * stride * 4UL,
                              &aBlk, &lBlk, &blk);
    if (r != IO_R_SUCCESS) {
        IOLog("OpenStepMGA D3-5a: could not map the window (%d)\n", (int)r);
        goto unmap;
    }
    if (!osmgaStormWaitIdle(base)) {
        IOLog("OpenStepMGA D3-5a: engine BUSY at entry\n");
        goto unmap;
    }

    for (row = 0UL; row < allRows; row++)
        for (col = 0UL; col < guardW; col++)
            blk[row * stride + col] = OSMGA_S1_SENTINEL;

    for (b = 0UL; b < OSMGA_D3_ALPHA_BANDS; b++) {
        unsigned long y0 = b * OSMGA_D3_ALPHA_ROWS;
        unsigned long ctrl, astart;

        if (b < 32UL)      { ctrl = blend | MGA_ALPHA_DIFFUSED;
                             astart = 1UL << b; }
        else if (b == 32UL){ ctrl = MGA_ALPHA_SRC_ONE | MGA_ALPHA_DST_ZERO |
                                    MGA_ALPHA_CHANNEL;  astart = 0UL; }
        else if (b == 33UL){ ctrl = 0UL;                astart = 0UL; }
        else if (b == 34UL){ ctrl = blend | MGA_ALPHA_DIFFUSED; astart = 0UL; }
        else if (b == 35UL){ ctrl = blend | MGA_ALPHA_DIFFUSED;
                             astart = 255UL << 15; }
        else if (b == 36UL){ ctrl = blend;              astart = 0UL; }
        else               { ctrl = blend;              astart = 255UL << 15; }

        if (!osmgaStormWaitFifo(base, 13U)) goto fifo;
        osmgaStormInitState(base, stride, 0UL, OSMGA_S1_W - 1UL,
                            y0 * stride,
                            (y0 + OSMGA_D3_ALPHA_ROWS - 1UL) * stride);

        if (!osmgaStormWaitFifo(base, 16U)) goto fifo;
        osmgaW32(base, MGA_DSTORG,      OSMGA_S1_VRAM_BLOCK);
        /* White source, so the result reads back as the alpha itself. */
        osmgaW32(base, MGA_DR4,  255UL << 15);
        osmgaW32(base, MGA_DR6,  0UL);
        osmgaW32(base, MGA_DR7,  0UL);
        osmgaW32(base, MGA_DR8,  255UL << 15);
        osmgaW32(base, MGA_DR10, 0UL);
        osmgaW32(base, MGA_DR11, 0UL);
        osmgaW32(base, MGA_DR12, 255UL << 15);
        osmgaW32(base, MGA_DR14, 0UL);
        osmgaW32(base, MGA_DR15, 0UL);
        osmgaW32(base, MGA_ALPHASTART, astart);
        osmgaW32(base, MGA_ALPHAXINC,  0UL);
        osmgaW32(base, MGA_ALPHAYINC,  0UL);
        /* Explicit, so a value left by the texture probes cannot take
         * part in selecting the alpha. */
        osmgaW32(base, MGA_TDUALSTAGE0, 0UL);
        osmgaW32(base, MGA_TDUALSTAGE1, 0UL);
        osmgaW32(base, MGA_ALPHACTRL,   ctrl);

        if (!osmgaStormWaitFifo(base, 4U)) goto fifo;
        osmgaW32(base, MGA_DWGCTL,  MGA_DWGCTL_GOURAUD);
        osmgaW32(base, MGA_FXBNDRY, (OSMGA_S1_W << 16) | 0UL);
        osmgaW32(base, MGA_YDSTLEN + MGA_EXEC,
                 (y0 << 16) | OSMGA_D3_ALPHA_ROWS);

        if (!osmgaStormWaitIdle(base)) {
            IOLog("OpenStepMGA D3-5a: band %lu did not finish\n", b);
            osmgaW32(base, MGA_DSTORG, 0UL);
            goto unmap;
        }
        osmgaW32(base, MGA_DSTORG, 0UL);
        seen[b] = blk[y0 * stride] & 0xffUL;      /* blue channel = alpha */
    }

    /* Leave the blend unit off for whatever runs next. */
    if (osmgaStormWaitFifo(base, 1U))
        osmgaW32(base, MGA_ALPHACTRL, 0UL);

    for (row = 0UL; row < allRows; row++)
        for (col = 0UL; col < guardW; col++) {
            if (row < drawRows && col < OSMGA_S1_W) continue;
            if (blk[row * stride + col] != OSMGA_S1_SENTINEL) cGuard++;
        }

    for (b = 0UL; b < 32UL; b++) {
        unsigned long v = seen[b], j = 0UL;

        if (v == 0UL) { map[b] = '.'; continue; }
        while ((v & 1UL) == 0UL) { v >>= 1; j++; }
        map[b] = "0123456789abcdef"[j & 15UL];
        if (v != 1UL && map[b] >= 'a')
            map[b] = (char)(map[b] - 'a' + 'A');
        else if (v != 1UL)
            map[b] = (char)(map[b] - '0' + 'G');
    }
    map[32] = '\0';

    IOLog("OpenStepMGA D3-5a: ALPHASTART bit 0..31 -> alpha bit  %s\n", map);
    IOLog("OpenStepMGA D3-5a: SRC_ONE %lu, blending off %lu (both want "
          "255)\n", seen[32], seen[33]);
    IOLog("OpenStepMGA D3-5a: DIFFUSED on a=0/255 -> %lu %lu; DIFFUSED off "
          "a=0/255 -> %lu %lu; colour guard %lu\n",
          seen[34], seen[35], seen[36], seen[37], cGuard);

    if (cGuard != 0UL)
        IOLog("OpenStepMGA D3-5a: STOP -- colour escaped the clip\n");
    else if (seen[33] != 255UL)
        IOLog("OpenStepMGA D3-5a: FAIL -- the band with blending off is not "
              "white, so the draw itself is wrong; ignore the rest\n");
    else if (seen[32] != 255UL)
        IOLog("OpenStepMGA D3-5a: FAIL -- SRC_ONE did not pass the source "
              "through, so the blend unit is not behaving as read\n");
    else if (seen[34] == 0UL && seen[35] == 255UL &&
             !(seen[36] == 0UL && seen[37] == 255UL))
        IOLog("OpenStepMGA D3-5a: PASS -- DIFFUSEDALPHA selects the "
              "ALPHASTART interpolator as the source alpha\n");
    else if (seen[34] == 0UL && seen[35] == 255UL &&
             seen[36] == 0UL && seen[37] == 255UL)
        IOLog("OpenStepMGA D3-5a: PASS -- the interpolator is always the "
              "source alpha; DIFFUSEDALPHA makes no difference here\n");
    else
        IOLog("OpenStepMGA D3-5a: the alpha does not follow ALPHASTART in "
              "either state -- read the four values above\n");
    goto unmap;

fifo:
    IOLog("OpenStepMGA D3-5a: fifo wait timed out in band %lu\n", b);
    osmgaW32(base, MGA_DSTORG, 0UL);

unmap:
    if (aBlk != 0) (void)IOUnmapPhysicalFromIOTask(aBlk, lBlk);
}


/*
 * D3-5b -- see the note by MGA_ALPHA_DST_1MSA.
 */
- (void)runAlphaBlendTest
{
    IODisplayInfo *di = [self displayInfo];
    const OSMGAFormat *f = &osmgaFmt[selectedFormatIndex];
    vm_address_t base = mmioBase;
    unsigned long stride = (unsigned long)di->rowBytes / 4UL;
    unsigned long linRows = 12UL * OSMGA_D3_ALPHA_ROWS;          /* 24 */
    unsigned long blendRows = 5UL * OSMGA_D3_BLEND_ROWS;         /* 20 */
    unsigned long drawRows = linRows + blendRows;                /* 44 */
    unsigned long allRows = drawRows + OSMGA_D3_ISO_GUARDROWS;
    unsigned long guardW = 2UL * OSMGA_S1_W;
    unsigned long blend = MGA_ALPHA_SRC_ALPHA | MGA_ALPHA_DST_ZERO |
                          MGA_ALPHA_CHANNEL;
    vm_address_t aBlk = 0, aSrc = 0;
    unsigned long lBlk = 0, lSrc = 0;
    volatile unsigned long *blk = 0, *src = 0;
    unsigned long b, row, col;
    unsigned long lin[12], mix[5], cGuard = 0UL;
    static const unsigned long interior[6] =
        { 3UL, 5UL, 0x55UL, 0xAAUL, 0x7FUL, 0xFEUL };
    static const unsigned long blendA[5] = { 0UL, 64UL, 128UL, 192UL, 255UL };
    IOReturn r;

    if (!rasterTestEnabled || !linearModeActive || !mmioMapped)
        return;
    if (f->bytesPerPixel != 4)
        return;
    if (OSMGA_S1_VRAM_BLOCK + allRows * stride * 4UL > OSMGA_D3_ZORG ||
        OSMGA_D3_BLEND_SRCORG + allRows * stride * 4UL > OSMGA_D3_TEXORG) {
        IOLog("OpenStepMGA D3-5b: windows do not fit, skipped\n");
        return;
    }

    r = osmgaMapUncachedBlock(frameBufferPhysical, OSMGA_S1_VRAM_BLOCK,
                              OSMGA_S1_VRAM_BLOCK + allRows * stride * 4UL,
                              &aBlk, &lBlk, &blk);
    if (r == IO_R_SUCCESS)
        r = osmgaMapUncachedBlock(frameBufferPhysical, OSMGA_D3_BLEND_SRCORG,
                                  OSMGA_D3_BLEND_SRCORG +
                                  allRows * stride * 4UL,
                                  &aSrc, &lSrc, &src);
    if (r != IO_R_SUCCESS) {
        IOLog("OpenStepMGA D3-5b: could not map the windows (%d)\n", (int)r);
        goto unmap;
    }
    if (!osmgaStormWaitIdle(base)) {
        IOLog("OpenStepMGA D3-5b: engine BUSY at entry\n");
        goto unmap;
    }

    for (row = 0UL; row < allRows; row++)
        for (col = 0UL; col < guardW; col++) {
            blk[row * stride + col] = (row >= linRows && row < drawRows)
                                    ? OSMGA_D3_BLEND_DSTVAL
                                    : OSMGA_S1_SENTINEL;
            src[row * stride + col] = OSMGA_D3_BLEND_SRCVAL;
        }

    /* Part 1 -- six interior alphas under each DIFFUSEDALPHA state. */
    for (b = 0UL; b < 12UL; b++) {
        unsigned long y0 = b * OSMGA_D3_ALPHA_ROWS;
        unsigned long ctrl = blend | ((b < 6UL) ? MGA_ALPHA_DIFFUSED : 0UL);
        unsigned long a = interior[b % 6UL];

        if (!osmgaStormWaitFifo(base, 13U)) goto fifo;
        osmgaStormInitState(base, stride, 0UL, OSMGA_S1_W - 1UL,
                            y0 * stride,
                            (y0 + OSMGA_D3_ALPHA_ROWS - 1UL) * stride);
        if (!osmgaStormWaitFifo(base, 16U)) goto fifo;
        osmgaW32(base, MGA_DSTORG, OSMGA_S1_VRAM_BLOCK);
        osmgaW32(base, MGA_DR4,  255UL << 15);
        osmgaW32(base, MGA_DR6,  0UL);
        osmgaW32(base, MGA_DR7,  0UL);
        osmgaW32(base, MGA_DR8,  255UL << 15);
        osmgaW32(base, MGA_DR10, 0UL);
        osmgaW32(base, MGA_DR11, 0UL);
        osmgaW32(base, MGA_DR12, 255UL << 15);
        osmgaW32(base, MGA_DR14, 0UL);
        osmgaW32(base, MGA_DR15, 0UL);
        osmgaW32(base, MGA_ALPHASTART,  a << 15);
        osmgaW32(base, MGA_ALPHAXINC,   0UL);
        osmgaW32(base, MGA_ALPHAYINC,   0UL);
        osmgaW32(base, MGA_TDUALSTAGE0, 0UL);
        osmgaW32(base, MGA_TDUALSTAGE1, 0UL);
        osmgaW32(base, MGA_ALPHACTRL,   ctrl);

        if (!osmgaStormWaitFifo(base, 4U)) goto fifo;
        osmgaW32(base, MGA_DWGCTL,  MGA_DWGCTL_GOURAUD);
        osmgaW32(base, MGA_FXBNDRY, (OSMGA_S1_W << 16) | 0UL);
        osmgaW32(base, MGA_YDSTLEN + MGA_EXEC,
                 (y0 << 16) | OSMGA_D3_ALPHA_ROWS);
        if (!osmgaStormWaitIdle(base)) goto busy;
        osmgaW32(base, MGA_DSTORG, 0UL);
        lin[b] = blk[y0 * stride] & 0xffUL;
    }

    /* Part 2 -- blend over a known destination, with SRCORG aimed at a
     * third block so the result says which origin the read followed. */
    for (b = 0UL; b < 5UL; b++) {
        unsigned long y0 = linRows + b * OSMGA_D3_BLEND_ROWS;

        if (!osmgaStormWaitFifo(base, 13U)) goto fifo;
        osmgaStormInitState(base, stride, 0UL, OSMGA_S1_W - 1UL,
                            y0 * stride,
                            (y0 + OSMGA_D3_BLEND_ROWS - 1UL) * stride);
        if (!osmgaStormWaitFifo(base, 16U)) goto fifo;
        osmgaW32(base, MGA_DSTORG, OSMGA_S1_VRAM_BLOCK);
        osmgaW32(base, MGA_SRCORG, OSMGA_D3_BLEND_SRCORG);
        osmgaW32(base, MGA_DR4,  ((OSMGA_D3_BLEND_COLOUR >> 16) & 0xffUL) << 15);
        osmgaW32(base, MGA_DR6,  0UL);
        osmgaW32(base, MGA_DR7,  0UL);
        osmgaW32(base, MGA_DR8,  ((OSMGA_D3_BLEND_COLOUR >> 8) & 0xffUL) << 15);
        osmgaW32(base, MGA_DR10, 0UL);
        osmgaW32(base, MGA_DR11, 0UL);
        osmgaW32(base, MGA_DR12, (OSMGA_D3_BLEND_COLOUR & 0xffUL) << 15);
        osmgaW32(base, MGA_DR14, 0UL);
        osmgaW32(base, MGA_DR15, 0UL);
        osmgaW32(base, MGA_ALPHASTART,  blendA[b] << 15);
        osmgaW32(base, MGA_ALPHAXINC,   0UL);
        osmgaW32(base, MGA_ALPHAYINC,   0UL);
        osmgaW32(base, MGA_ALPHACTRL,   MGA_ALPHA_SRC_ALPHA |
                                        MGA_ALPHA_DST_1MSA |
                                        MGA_ALPHA_CHANNEL);

        if (!osmgaStormWaitFifo(base, 4U)) goto fifo;
        osmgaW32(base, MGA_DWGCTL,  MGA_DWGCTL_GOURAUD);
        osmgaW32(base, MGA_FXBNDRY, (OSMGA_S1_W << 16) | 0UL);
        osmgaW32(base, MGA_YDSTLEN + MGA_EXEC,
                 (y0 << 16) | OSMGA_D3_BLEND_ROWS);
        if (!osmgaStormWaitIdle(base)) goto busy;
        osmgaW32(base, MGA_DSTORG, 0UL);
        osmgaW32(base, MGA_SRCORG, 0UL);
        mix[b] = blk[y0 * stride] & 0xffffffUL;
    }

    if (osmgaStormWaitFifo(base, 1U))
        osmgaW32(base, MGA_ALPHACTRL, 0UL);

    for (row = 0UL; row < allRows; row++)
        for (col = 0UL; col < guardW; col++) {
            unsigned long want = (row >= linRows && row < drawRows)
                               ? OSMGA_D3_BLEND_DSTVAL : OSMGA_S1_SENTINEL;

            if (row < drawRows && col < OSMGA_S1_W) continue;
            if (blk[row * stride + col] != want) cGuard++;
        }

    IOLog("OpenStepMGA D3-5b: interior alphas 3,5,55,aa,7f,fe -- DIFFUSED "
          "on -> %lx %lx %lx %lx %lx %lx\n",
          lin[0], lin[1], lin[2], lin[3], lin[4], lin[5]);
    IOLog("OpenStepMGA D3-5b: the same six, DIFFUSED off -> %lx %lx %lx "
          "%lx %lx %lx\n",
          lin[6], lin[7], lin[8], lin[9], lin[10], lin[11]);
    IOLog("OpenStepMGA D3-5b: blend a=0,64,128,192,255 -> %06lx %06lx "
          "%06lx %06lx %06lx\n", mix[0], mix[1], mix[2], mix[3], mix[4]);
    IOLog("OpenStepMGA D3-5b: src %06lx over dst %06lx; SRCORG block holds "
          "%06lx; guard %lu\n", OSMGA_D3_BLEND_COLOUR,
          OSMGA_D3_BLEND_DSTVAL, OSMGA_D3_BLEND_SRCVAL, cGuard);

    if (cGuard != 0UL)
        IOLog("OpenStepMGA D3-5b: STOP -- a write escaped the clip\n");
    else if (lin[0] != interior[0] || lin[5] != interior[5])
        IOLog("OpenStepMGA D3-5b: FAIL -- mixed-bit alphas do not come back "
              "unchanged, so the alpha path is not linear\n");
    else if (mix[0] != OSMGA_D3_BLEND_DSTVAL ||
             mix[4] != OSMGA_D3_BLEND_COLOUR)
        IOLog("OpenStepMGA D3-5b: FAIL -- the blend endpoints are wrong; "
              "a=0 must leave the destination and a=255 must replace it\n");
    else
        IOLog("OpenStepMGA D3-5b: endpoints hold; compare the interior "
              "values and the two DIFFUSED rows above\n");
    goto unmap;

busy:
    IOLog("OpenStepMGA D3-5b: band %lu did not finish\n", b);
    osmgaW32(base, MGA_DSTORG, 0UL);
    osmgaW32(base, MGA_SRCORG, 0UL);
    goto unmap;

fifo:
    IOLog("OpenStepMGA D3-5b: fifo wait timed out in band %lu\n", b);
    osmgaW32(base, MGA_DSTORG, 0UL);
    osmgaW32(base, MGA_SRCORG, 0UL);

unmap:
    if (aSrc != 0) (void)IOUnmapPhysicalFromIOTask(aSrc, lSrc);
    if (aBlk != 0) (void)IOUnmapPhysicalFromIOTask(aBlk, lBlk);
}



/* Ceiling log2, the same thing MGA_LOG2 and GetPowerOfTwo compute: the
 * log2 field says which power of two CONTAINS the texture, and the exact
 * size travels separately, so a non-power-of-two size is legal. */
static unsigned long
osmgaHW3DLog2Ceil(unsigned long n)
{
    unsigned long l = 0UL;

    while ((1UL << l) < n && l < 31UL)
        l++;
    return l;
}

/*
 * ---- M1-2a: turn a validated batch into a DMA command list ----
 *
 * The engine state that bounds a draw -- PITCH, the clip, MACCESS -- is set
 * by MMIO before the list is submitted and never appears in the list, so a
 * batch cannot move the walls it is drawn inside.  What the list carries is
 * the origins, the interpolators and the edges, which is exactly the set
 * the validator bounds.
 *
 * EXEC goes last in its block: the values in a block are applied in index
 * order, and everything the triangle needs must be in place before the
 * write that starts it.
 */
static unsigned long
osmgaHW3DEncode(unsigned long *list, unsigned long listDwords,
                const OSMGAHW3DBatch *b, unsigned long *outTail)
{
    unsigned long pos = 0UL, i;
    int anyZI = 0, anyTex = 0;
    int ok = 1;

    for (i = 0UL; i < b->triCount; i++) {
        unsigned long d = b->tri[i].dwgctl & OSMGA_HW3D_DWG_CLIENT;

        if (((d >> 4) & 0x7UL) == OSMGA_HW3D_ATYPE_ZI) anyZI = 1;
        if ((d & 0xFUL) == OSMGA_HW3D_OPCODE_TEX)      anyTex = 1;
    }

    /* When no triangle addresses depth, ZORG should still point somewhere
     * harmless rather than at zero: zero is the visible framebuffer, so
     * the one case the validator does not check is the one that would be
     * worst if a depth write happened anyway.  "Not addressed" is a claim
     * about the hardware, and this makes it not matter. */
    ok = ok && osmgaDmaBlock(list, listDwords, &pos,
                             MGA_DSTORG,   b->state.dstorg,
                             MGA_ZORG,     anyZI ? b->state.zorg
                                                 : OSMGA_D3_ZORG,
                             MGA_DMAPAD,   0UL,
                             MGA_DMAPAD,   0UL);

    /*
     * Texture state, only when something is textured.  The client gave a
     * size, a pitch and a format; every register here is built from those
     * rather than handed over, which is what keeps CLAMPUV ours -- and the
     * reason the coordinate matrix needs no validation at all is that
     * CLAMPUV was measured to bound the fetched address, not just the
     * coordinate.
     *
     * Emitting nothing when nothing is textured is not an optimisation:
     * a batch that leaves these registers alone inherits whatever ran
     * before it, so the untextured case has to be untextured by its
     * DWGCTL opcode rather than by hoping the state is stale in a
     * convenient way.
     */
    if (ok && anyTex) {
        unsigned long w = b->state.texW, h = b->state.texH;
        unsigned long lw = osmgaHW3DLog2Ceil(w), lh = osmgaHW3DLog2Ceil(h);

        ok = ok && osmgaDmaBlock(list, listDwords, &pos,
                 MGA_TEXORG,    b->state.texorg,
                 MGA_TEXWIDTH,  ((w - 1UL) << 18) |
                                (((8UL - lw) & 63UL) << 9) | lw,
                 MGA_TEXHEIGHT, ((h - 1UL) << 18) |
                                (((8UL - lh) & 63UL) << 9) | lh,
                 MGA_TEXCTL,    MGA_TEXCTL_PITCHLIN |
                                ((b->state.texPitch & 2047UL) << 9) |
                                MGA_TEXCTL_NOPERSP | MGA_TEXCTL_TAKEY |
                                MGA_TEXCTL_CLAMPUV | MGA_TEXCTL_TW32);
        ok = ok && osmgaDmaBlock(list, listDwords, &pos,
                 MGA_TEXCTL2,      MGA_TEXCTL2_G400_MAGIC |
                                   MGA_TEXCTL2_CKSTRANSDIS,
                 MGA_TEXFILTER,    MGA_TEXFILTER_ALPHA | (0x10UL << 21) |
                                   (((b->state.texFlags &
                                      OSMGA_HW3D_TEXF_BILIN) != 0UL)
                                    ? 0x20UL : 0UL),
                 MGA_TEXTRANS,     0x0000ffffUL,
                 MGA_TEXTRANSHIGH, 0x0000ffffUL);
        ok = ok && osmgaDmaBlock(list, listDwords, &pos,
                 MGA_TDUALSTAGE0, 0UL, MGA_TDUALSTAGE1, 0UL,
                 MGA_TMR0,        (unsigned long)b->state.tmr[0],
                 MGA_TMR0 +  4UL, (unsigned long)b->state.tmr[1]);
        /* The H family is the kernel's: we set NOPERSPECTIVE, nothing in
         * the sources says these are then ignored, and a client value here
         * would be one more thing the coordinate bound does not cover. */
        ok = ok && osmgaDmaBlock(list, listDwords, &pos,
                 MGA_TMR0 +  8UL, (unsigned long)b->state.tmr[2],
                 MGA_TMR0 + 12UL, (unsigned long)b->state.tmr[3],
                 MGA_TMR0 + 16UL, 0UL,
                 MGA_TMR0 + 20UL, 0UL);
        ok = ok && osmgaDmaBlock(list, listDwords, &pos,
                 MGA_TMR0 + 24UL, (unsigned long)b->state.tmr[6],
                 MGA_TMR0 + 28UL, (unsigned long)b->state.tmr[7],
                 MGA_TMR8,        1UL << 16,
                 MGA_DMAPAD,      0UL);
    }

    for (i = 0UL; ok && i < b->triCount; i++) {
        const OSMGAHW3DTri *t = &b->tri[i];

        /* The client supplies opcode, access type and z mode; every other
         * bit comes from here, so bits it never reasoned about are not
         * bits it can set.  The sloped form then clears ARZERO and
         * SGNZERO, which are ours to clear because they are not in the
         * client's mask either. */
        unsigned long dwg = OSMGA_HW3D_DWG_FIXED |
                            (t->dwgctl & OSMGA_HW3D_DWG_CLIENT);

        ok = ok && osmgaDmaBlock(list, listDwords, &pos,
                                 MGA_DWGCTL, MGA_DWGCTL_SLOPED(dwg),
                                 MGA_AR0, (unsigned long)t->ar0,
                                 MGA_AR1, (unsigned long)t->ar1,
                                 MGA_AR2, (unsigned long)t->ar2);
        ok = ok && osmgaDmaBlock(list, listDwords, &pos,
                                 MGA_AR4, (unsigned long)t->ar4,
                                 MGA_AR5, (unsigned long)t->ar5,
                                 MGA_AR6, (unsigned long)t->ar6,
                                 MGA_SGN, (unsigned long)t->sgn);
        ok = ok && osmgaDmaBlock(list, listDwords, &pos,
                                 MGA_DR4,  t->dr[0], MGA_DR6,  t->dr[1],
                                 MGA_DR7,  t->dr[2], MGA_DR8,  t->dr[3]);
        ok = ok && osmgaDmaBlock(list, listDwords, &pos,
                                 MGA_DR10, t->dr[4], MGA_DR11, t->dr[5],
                                 MGA_DR12, t->dr[6], MGA_DR14, t->dr[7]);
        ok = ok && osmgaDmaBlock(list, listDwords, &pos,
                                 MGA_DR15, t->dr[8],
                                 MGA_DR0,  t->z0,
                                 MGA_DR2,  t->zdx,
                                 MGA_DR3,  t->zdy);
        ok = ok && osmgaDmaBlock(list, listDwords, &pos,
                                 MGA_ALPHASTART, t->a0,
                                 MGA_ALPHAXINC,  t->adx,
                                 MGA_ALPHAYINC,  t->ady,
                                 MGA_ALPHACTRL,
                                 t->alphactrl & OSMGA_HW3D_AC_CLIENT);
        ok = ok && osmgaDmaBlock(list, listDwords, &pos,
                                 MGA_DMAPAD, 0UL, MGA_DMAPAD, 0UL,
                                 MGA_FXBNDRY, t->fxbndry,
                                 MGA_YDSTLEN + MGA_EXEC,
                                 (((unsigned long)t->y) << 16) |
                                 ((unsigned long)t->h));
    }

    /* Leave DWGCTL in a state nothing inherits by accident. */
    ok = ok && osmgaDmaBlock(list, listDwords, &pos,
                             MGA_DMAPAD, 0UL, MGA_DMAPAD, 0UL,
                             MGA_DWGCTL, MGA_DWGCTL_GOURAUD,
                             MGA_DMAPAD, 0UL);
    /* The trap has to be INSIDE what PRIMEND covers, and the card reads a
     * little past PRIMEND, so a padding block has to follow it.  Getting
     * either the wrong way round leaves the list running to the end with
     * the trap never fired -- which is exactly what the first attempt did,
     * and what STATUS bit 0 reported. */
    ok = ok && osmgaDmaBlock(list, listDwords, &pos,
                             MGA_DMAPAD, 0UL, MGA_DMAPAD, 0UL,
                             MGA_DMAPAD, 0UL, MGA_SOFTRAP, 0UL);
    if (!ok)
        return 0UL;
    if (outTail != 0)
        *outTail = pos;                 /* PRIMEND points here */
    if (!osmgaDmaBlock(list, listDwords, &pos,
                       MGA_DMAPAD, 0UL, MGA_DMAPAD, 0UL,
                       MGA_DMAPAD, 0UL, MGA_DMAPAD, 0UL))
        return 0UL;
    return pos;
}

/*
 * M1-0 -- exercise the mmap decision directly.
 *
 * osmgaDevMmap is a pure function, so every boundary can be checked by
 * calling it rather than by mapping anything, which keeps a test of the
 * one routine that can expose arbitrary physical memory entirely free of
 * risk.  Repeated calls for one offset are included because the kernel
 * makes them and does not check the second return.
 */
- (void)runMmapWindowTest
{
    unsigned long cmdBase = OSMGA_CMD_MMAP_BASE;
    unsigned long vs = osmgaMmapWindowStart, ve = osmgaMmapWindowEnd;
    unsigned long cb = osmgaMmapCmdBytes;
    int bad = 0, n = 0;
    struct { unsigned long off; int want; const char *what; } t[14];
    int i;

    if (!osmgaMmapRegistered)
        return;

    /* want: 1 accept, 0 refuse */
    t[n].off = vs;                    t[n].want = 1; t[n++].what = "vram first";
    t[n].off = vs + (unsigned long)PAGE_SIZE;
                                      t[n].want = 1; t[n++].what = "vram +1pg";
    t[n].off = ve - (unsigned long)PAGE_SIZE;
                                      t[n].want = 1; t[n++].what = "vram last";
    t[n].off = ve;                    t[n].want = 0; t[n++].what = "vram end";
    t[n].off = ve + (unsigned long)PAGE_SIZE;
                                      t[n].want = 0; t[n++].what = "past vram";
    t[n].off = (vs >= (unsigned long)PAGE_SIZE)
                   ? vs - (unsigned long)PAGE_SIZE : 0UL;
                                      t[n].want = 0; t[n++].what = "before vram";
    t[n].off = vs + 1UL;              t[n].want = 0; t[n++].what = "vram unaligned";
    t[n].off = cmdBase - (unsigned long)PAGE_SIZE;
                                      t[n].want = 0; t[n++].what = "below cmd";
    t[n].off = cmdBase;               t[n].want = (cb != 0UL);
                                                     t[n++].what = "cmd first";
    t[n].off = cmdBase + cb - (unsigned long)PAGE_SIZE;
                                      t[n].want = (cb != 0UL);
                                                     t[n++].what = "cmd last";
    t[n].off = cmdBase + cb;          t[n].want = 0; t[n++].what = "cmd end";
    t[n].off = cmdBase + cb + (unsigned long)PAGE_SIZE;
                                      t[n].want = 0; t[n++].what = "past cmd";
    t[n].off = cmdBase + 1UL;         t[n].want = 0; t[n++].what = "cmd unaligned";
    t[n].off = 0UL;                   t[n].want = (vs == 0UL);
                                                     t[n++].what = "offset zero";

    for (i = 0; i < n; i++) {
        int a = osmgaDevMmap(0, (int)t[i].off, OSMGA_PROT_RW);
        int b = osmgaDevMmap(0, (int)t[i].off, OSMGA_PROT_RW);
        int accepted = (a >= 0);

        if (a != b) {                       /* the kernel trusts the second */
            IOLog("OpenStepMGA M1-0: NOT DETERMINISTIC at %s (%08lx): "
                  "%d then %d\n", t[i].what, t[i].off, a, b);
            bad++;
        }
        if (accepted != (t[i].want != 0)) {
            IOLog("OpenStepMGA M1-0: %s (%08lx) -> %d, wanted %s\n",
                  t[i].what, t[i].off, a, t[i].want ? "accept" : "refuse");
            bad++;
        }
    }

    /* A wrong protection must be refused wherever it is asked. */
    if (osmgaDevMmap(0, (int)vs, PROT_READ) >= 0 ||
        osmgaDevMmap(0, (int)cmdBase, PROT_READ) >= 0) {
        IOLog("OpenStepMGA M1-0: read-only protection was accepted\n");
        bad++;
    }

    IOLog("OpenStepMGA M1-0: vram %08lx..%08lx, cmd base %08lx bytes %lu "
          "phys %08lx; %d cases, %d wrong\n",
          vs, ve, cmdBase, cb, osmgaMmapCmdPhysical, n, bad);
    if (bad == 0)
        IOLog("OpenStepMGA M1-0: PASS -- both windows decide as specified "
              "and repeat themselves\n");
    else
        IOLog("OpenStepMGA M1-0: FAIL -- see the cases above; do not map "
              "anything until this is clean\n");
}


/*
 * M1-2a -- the same triangle twice: once by MMIO, once through a validated
 * batch turned into a DMA list.
 *
 * Userland is not involved yet.  Putting the client, the RPC and the DMA
 * encoding in at once would leave three possible causes for one failure,
 * which is the mistake D3-3a made by writing a depth probe before the
 * depth layout was known.  Here the kernel plays the client, so a
 * mismatch can only be the encoding or the submission.
 *
 * This also re-exercises the DMA ring for the first time since the
 * encoder stopped accepting the secondary and setup address registers.
 */
- (void)runHW3DBatchTest
{
    IODisplayInfo *di = [self displayInfo];
    const OSMGAFormat *f = &osmgaFmt[selectedFormatIndex];
    vm_address_t base = mmioBase;
    unsigned long stride = (unsigned long)di->rowBytes / 4UL;
    unsigned long band = OSMGA_D3_4B_BAND;               /* 20 rows */
    unsigned long allRows = 2UL * band + OSMGA_D3_ISO_GUARDROWS;
    unsigned long guardW = 2UL * OSMGA_S1_W;
    OSMGAHW3DBatch *batch;
    OSMGAHW3DLimits lim;
    unsigned long *list, listDwords, listPhys, total, tail;
    vm_address_t aBlk = 0;
    unsigned long lBlk = 0;
    volatile unsigned long *blk = 0;
    unsigned long row, col, diff = 0UL, drewRef = 0UL, drewDma = 0UL;
    unsigned long diffLow = 0UL, firstA = 0UL, firstB = 0UL, firstAt = 0UL;
    unsigned long cGuard = 0UL, spins, status;
    unsigned long badTri = 0UL;
    int v;
    IOReturn r;

    /* "Raster Test" asks for engine probes; "Mesa Acceleration" decides
     * whether the 3D client path runs at all.  This test drives that path,
     * so it answers to both -- otherwise a machine with acceleration turned
     * off would still start 3D DMA of its own accord at every boot, which
     * is exactly what the switch is supposed to stop. */
    if (!osmgaMesaAccelEnabled)
        return;
    if (!rasterTestEnabled || !linearModeActive || !mmioMapped)
        return;
    if (f->bytesPerPixel != 4)
        return;
    if (osmgaMmapCmdVirt == 0 || osmgaMmapCmdPhysical == 0UL) {
        IOLog("OpenStepMGA M1-2a: no command ring, skipped\n");
        return;
    }
    if (OSMGA_S1_VRAM_BLOCK + allRows * stride * 4UL > OSMGA_D3_ZORG) {
        IOLog("OpenStepMGA M1-2a: window does not fit, skipped\n");
        return;
    }

    batch = (OSMGAHW3DBatch *)osmgaMmapCmdVirt;
    list = (unsigned long *)((char *)osmgaMmapCmdVirt +
                             OSMGA_HW3D_RING_OFFSET);
    listDwords = (OSMGA_DMA_RING_BYTES - OSMGA_HW3D_RING_OFFSET) / 4UL;
    listPhys = osmgaMmapCmdPhysical + OSMGA_HW3D_RING_OFFSET;

    r = osmgaMapUncachedBlock(frameBufferPhysical, OSMGA_S1_VRAM_BLOCK,
                              OSMGA_S1_VRAM_BLOCK + allRows * stride * 4UL,
                              &aBlk, &lBlk, &blk);
    if (r != IO_R_SUCCESS) {
        IOLog("OpenStepMGA M1-2a: could not map the window (%d)\n", (int)r);
        return;
    }
    if (!osmgaStormWaitIdle(base)) {
        IOLog("OpenStepMGA M1-2a: engine BUSY at entry\n");
        goto unmap;
    }
    for (row = 0UL; row < allRows; row++)
        for (col = 0UL; col < guardW; col++)
            blk[row * stride + col] = OSMGA_S1_SENTINEL;

    /* What the kernel owns.  None of it is reachable from a batch. */
    lim.pitchBytes  = (unsigned long)di->rowBytes;
    lim.clipY1      = 2UL * band - 1UL;
    lim.clipX1      = OSMGA_S1_W - 1UL;
    lim.colourStart = OSMGA_S1_VRAM_BLOCK;
    lim.colourEnd   = OSMGA_D3_ZORG;
    lim.depthStart  = OSMGA_D3_ZORG;
    lim.depthEnd    = OSMGA_D3_TEXORG;
    lim.texStart    = OSMGA_D3_TEXORG;
    lim.texEnd      = OSMGA_S1_VRAM_PROVEN;
    lim.batchBytes  = OSMGA_HW3D_BATCH_BYTES;
    lim.maxEdgeWalk = OSMGA_HW3D_EDGE_WALK;


    /* Reference: MMIO, rows 0..19. */
    if (!osmgaStormWaitFifo(base, 13U)) goto unmap;
    osmgaStormInitState(base, stride, 0UL, OSMGA_S1_W - 1UL,
                        0UL, (2UL * band - 1UL) * stride);
    if (!osmgaStormWaitFifo(base, 12U)) goto unmap;
    osmgaW32(base, MGA_DSTORG,    OSMGA_S1_VRAM_BLOCK);
    osmgaW32(base, MGA_ALPHACTRL, MGA_ALPHACTRL_OPAQUE);
    osmgaW32(base, MGA_DR4,  200UL << 15); osmgaW32(base, MGA_DR6,  0UL);
    osmgaW32(base, MGA_DR7,  0UL);
    osmgaW32(base, MGA_DR8,  100UL << 15); osmgaW32(base, MGA_DR10, 0UL);
    osmgaW32(base, MGA_DR11, 0UL);
    osmgaW32(base, MGA_DR12,  50UL << 15); osmgaW32(base, MGA_DR14, 0UL);
    osmgaW32(base, MGA_DR15, 0UL);
    /* The two paths have to write the SAME SET of registers, not merely
     * the same values in the ones they share.  Anything a path leaves out
     * is inherited from whatever ran before it -- the DRM's own context
     * struct tracks DSTORG and ALPHACTRL but not the alpha interpolator,
     * so that class of leak is how the reference implementation behaves,
     * not a quirk of ours.  These four are what the DMA list writes and
     * this path did not. */
    if (!osmgaStormWaitFifo(base, 8U)) goto unmap;
    osmgaW32(base, MGA_ALPHASTART, 0UL);
    osmgaW32(base, MGA_ALPHAXINC,  0UL);
    osmgaW32(base, MGA_ALPHAYINC,  0UL);
    osmgaW32(base, MGA_ZORG, 0UL);
    osmgaW32(base, MGA_DR0,  0UL);
    osmgaW32(base, MGA_DR2,  0UL);
    osmgaW32(base, MGA_DR3,  0UL);
    if (!osmgaStormWaitFifo(base, 11U)) goto unmap;
    osmgaStormTrap(base, MGA_DWGCTL_GOURAUD, 0UL, band,
                   0L, OSMGA_D3_4B_SLOPE, (long)band, 0L,
                   (long)(OSMGA_S1_W - 1UL), 0L, (long)band, 0L);
    if (!osmgaStormWaitIdle(base)) {
        IOLog("OpenStepMGA M1-2a: the MMIO reference did not finish\n");
        goto unmap;
    }

    /* The batch: the same shape, one band lower.  These are the values
     * osmgaStormTrap derives, computed here because a client would. */
    {
        long dxL = OSMGA_D3_4B_SLOPE;
        OSMGAHW3DTri *t;

        for (col = 0UL; col < sizeof(OSMGAHW3DBatch) / 4UL; col++)
            ((unsigned long *)batch)[col] = 0UL;
        batch->magic = OSMGA_HW3D_MAGIC;
        batch->version = OSMGA_HW3D_VERSION;
        batch->triCount = 1UL;
        batch->state.dstorg = OSMGA_S1_VRAM_BLOCK;
        /*
         * The geometry the validator now requires.  It has to be set AFTER
         * the batch is zeroed, not before: putting it above the clearing
         * loop compiles, changes nothing, and leaves every batch here
         * failing on the pitch before it reaches what the test is for.
         */
        batch->state.dstPitch  = stride;
        batch->state.dstWidth  = lim.clipX1 + 1UL;
        batch->state.dstHeight = lim.clipY1 + 1UL;
        t = &batch->tri[0];
        t->dwgctl = OSMGA_HW3D_OPCODE_TRAP | (OSMGA_HW3D_ATYPE_I << 4);
        t->alphactrl = MGA_ALPHACTRL_OPAQUE;
        t->y = (long)band;
        t->h = (long)band;
        t->ar0 = (long)band;                 /* dyL */
        t->ar1 = -dxL;                       /* ar2 - eL */
        t->ar2 = -dxL;
        t->ar4 = 0L;
        t->ar5 = 0L;
        t->ar6 = (long)band;                 /* dyR */
        t->sgn = 0L;
        t->fxbndry = ((unsigned long)OSMGA_S1_W << 16) | 0UL;
        t->dr[0] = 200UL << 15;
        t->dr[3] = 100UL << 15;
        t->dr[6] =  50UL << 15;
    }

    v = osmgaHW3DValidate(batch, &lim, &badTri);
    if (v != OSMGA_HW3D_OK) {
        IOLog("OpenStepMGA M1-2a: the good batch was refused (%d, tri %lu)\n",
              v, badTri);
        goto unmap;
    }
    total = osmgaHW3DEncode(list, listDwords, batch, &tail);
    if (total == 0UL) {
        IOLog("OpenStepMGA M1-2a: encoding failed\n");
        goto unmap;
    }

    osmgaW32(base, MGA_ICLEAR, MGA_SOFTRAPICLR);

    /* Before touching the ring, wait for the DMA engine itself to be
     * quiescent, which is a different question from the drawing engine
     * being idle.  The DRM does this ahead of every flush, and the shape
     * of the test is its own: with the trap cleared, quiescent means the
     * status masks down to exactly ENDPRDMASTS. */
    for (spins = 0UL; spins < OSMGA_S1_SPIN_LIMIT; spins++) {
        status = osmgaR32(base, MGA_ENGSTATUS) & MGA_DMA_DONE_MASK;
        if (status == MGA_STATUS_ENDPRDMASTS)
            break;
    }
    if (spins >= OSMGA_S1_SPIN_LIMIT) {
        IOLog("OpenStepMGA M1-2a: DMA was not quiescent before submitting "
              "(status %08lx); the ring is NOT touched\n", status);
        goto unmap;
    }

    {   unsigned long sum = 0UL, i;

        for (i = 0UL; i < total; i++) sum += list[i];
        (void)osmgaR32(base, MGA_ENGSTATUS);
        if (sum == 0xFFFFFFFFUL) IOLog("barrier %lu\n", sum);
    }
    osmgaW32(base, MGA_PRIMADDRESS, listPhys | MGA_DMA_GENERAL);
    osmgaW32(base, MGA_PRIMEND, (listPhys + tail * 4UL) | MGA_DMA_GENERAL);
    for (spins = 0UL; spins < OSMGA_S1_SPIN_LIMIT; spins++) {
        status = osmgaR32(base, MGA_ENGSTATUS);
        if ((status & MGA_DMA_DONE_MASK) == MGA_DMA_DONE_VALUE)
            break;
    }
    osmgaW32(base, MGA_ICLEAR, MGA_SOFTRAPICLR);
    if (spins >= OSMGA_S1_SPIN_LIMIT) {
        IOLog("OpenStepMGA M1-2a: DMA did not report idle (status %08lx); "
              "the ring is NOT reused\n", status);
        goto unmap;
    }
    if (!osmgaStormWaitIdle(base)) {
        IOLog("OpenStepMGA M1-2a: engine still busy after the trap\n");
        goto unmap;
    }

    for (row = 0UL; row < band; row++)
        for (col = 0UL; col < OSMGA_S1_W; col++) {
            unsigned long a = blk[row * stride + col];
            unsigned long b2 = blk[(row + band) * stride + col];

            if (a != OSMGA_S1_SENTINEL) drewRef++;
            if (b2 != OSMGA_S1_SENTINEL) drewDma++;
            if (a != b2) {
                if (diff == 0UL) {
                    firstA = a; firstB = b2;
                    firstAt = row * 1000UL + col;
                }
                diff++;
                /* Splitting the difference by byte says whether the two
                 * paths disagree about the colour or only about the byte
                 * nothing displays. */
                if ((a & 0x00FFFFFFUL) != (b2 & 0x00FFFFFFUL))
                    diffLow++;
            }
        }
    for (row = 0UL; row < allRows; row++)
        for (col = 0UL; col < guardW; col++) {
            if (row < 2UL * band && col < OSMGA_S1_W) continue;
            if (blk[row * stride + col] != OSMGA_S1_SENTINEL) cGuard++;
        }

    /* Negative control: the one thing the validator exists to stop. */
    batch->state.dstorg = 0UL;
    v = osmgaHW3DValidate(batch, &lim, &badTri);

    IOLog("OpenStepMGA M1-2a: list %lu dwords (tail %lu); MMIO drew %lu, "
          "DMA drew %lu; differing %lu of which %lu differ below the top "
          "byte; guard %lu; spins %lu\n",
          total, tail, drewRef, drewDma, diff, diffLow, cGuard, spins);
    if (diff != 0UL)
        IOLog("OpenStepMGA M1-2a: first difference at row %lu col %lu -- "
              "MMIO %08lx, DMA %08lx\n",
              firstAt / 1000UL, firstAt % 1000UL, firstA, firstB);
    IOLog("OpenStepMGA M1-2a: dstorg aimed at the visible framebuffer -> "
          "validator says %d (4 = refused)\n", v);

    if (cGuard != 0UL)
        IOLog("OpenStepMGA M1-2a: STOP -- a write escaped the clip\n");
    else if (v != OSMGA_HW3D_E_DSTORG)
        IOLog("OpenStepMGA M1-2a: STOP -- the validator did not refuse a "
              "destination in the visible framebuffer\n");
    else if (drewRef == 0UL)
        IOLog("OpenStepMGA M1-2a: FAIL -- the MMIO reference drew nothing, "
              "so there is nothing to compare against\n");
    else if (diff == 0UL)
        IOLog("OpenStepMGA M1-2a: PASS -- the batch drew exactly what MMIO "
              "drew, pixel for pixel\n");
    else
        IOLog("OpenStepMGA M1-2a: FAIL -- the two paths differ; read the "
              "counts above\n");

unmap:
    if (aBlk != 0) (void)IOUnmapPhysicalFromIOTask(aBlk, lBlk);
}


/* Fill a batch triangle the way osmgaStormTrap derives its registers, so a
 * hostile edge is expressed exactly as a client would express it. */
static void
osmgaM1cTri(OSMGAHW3DTri *t, unsigned long y, unsigned long h,
            long left, long dxL, long dyL, long right, long dxR, long dyR)
{
    int sdxl = (dxL < 0) ? 1 : 0;
    int sdxr = (dxR < 0) ? 1 : 0;
    long ar2 = sdxl ? dxL : -dxL;
    long ar5 = sdxr ? dxR : -dxR;
    unsigned long i;

    for (i = 0UL; i < sizeof(OSMGAHW3DTri) / 4UL; i++)
        ((unsigned long *)t)[i] = 0UL;
    t->y = (long)y;
    t->h = (long)h;
    t->ar0 = dyL;
    t->ar1 = ar2;
    t->ar2 = ar2;
    t->ar4 = ar5;
    t->ar5 = ar5;
    t->ar6 = dyR;
    t->sgn = ((long)sdxl << 1) | ((long)sdxr << 5);
    t->fxbndry = (((unsigned long)(right + 1L)) << 16) |
                 ((unsigned long)left & 0xffffUL);
    t->dwgctl = OSMGA_HW3D_OPCODE_TRAP | (OSMGA_HW3D_ATYPE_I << 4);
    t->alphactrl = MGA_ALPHACTRL_OPAQUE;
    t->dr[0] = 200UL << 15;
    t->dr[3] = 100UL << 15;
    t->dr[6] =  50UL << 15;
}

/*
 * M1-2c -- see the note by OSMGA_M1C_DSTORG.
 */
- (void)runHW3DContainmentTest
{
    IODisplayInfo *di = [self displayInfo];
    const OSMGAFormat *f = &osmgaFmt[selectedFormatIndex];
    vm_address_t base = mmioBase;
    unsigned long stride = (unsigned long)di->rowBytes / 4UL;
    unsigned long bands = 6UL, band = OSMGA_M1C_BAND;
    unsigned long drawRows = bands * band;
    unsigned long lo = OSMGA_M1C_DSTORG - OSMGA_M1C_MARGIN_ROWS * stride * 4UL;
    unsigned long hi = OSMGA_M1C_DSTORG +
                       (drawRows + OSMGA_M1C_MARGIN_ROWS) * stride * 4UL;
    unsigned long winRows = (hi - lo) / (stride * 4UL);
    unsigned long dstRow = OSMGA_M1C_MARGIN_ROWS;      /* DSTORG row in the map */
    OSMGAHW3DBatch *batch;
    OSMGAHW3DLimits lim;
    unsigned long *list, listDwords, listPhys, total, tail;
    vm_address_t aWin = 0;
    unsigned long lWin = 0;
    volatile unsigned long *win = 0;
    unsigned long b, row, col, spins, status;
    unsigned long inside[6], outside = 0UL, firstBad = 0xFFFFFFFFUL;
    unsigned long row0[6];
    unsigned long badTri = 0UL;
    int v;
    IOReturn r;

    /* "Raster Test" asks for engine probes; "Mesa Acceleration" decides
     * whether the 3D client path runs at all.  This test drives that path,
     * so it answers to both -- otherwise a machine with acceleration turned
     * off would still start 3D DMA of its own accord at every boot, which
     * is exactly what the switch is supposed to stop. */
    if (!osmgaMesaAccelEnabled)
        return;
    if (!rasterTestEnabled || !linearModeActive || !mmioMapped)
        return;
    if (f->bytesPerPixel != 4)
        return;
    if (osmgaMmapCmdVirt == 0 || osmgaMmapCmdPhysical == 0UL) {
        IOLog("OpenStepMGA M1-2c: no command ring, skipped\n");
        return;
    }
    if (lo < OSMGA_S1_VRAM_BLOCK || hi > OSMGA_S1_VRAM_PROVEN) {
        IOLog("OpenStepMGA M1-2c: canary would leave the proven window, "
              "skipped\n");
        return;
    }

    batch = (OSMGAHW3DBatch *)osmgaMmapCmdVirt;
    list = (unsigned long *)((char *)osmgaMmapCmdVirt +
                             OSMGA_HW3D_RING_OFFSET);
    listDwords = (OSMGA_DMA_RING_BYTES - OSMGA_HW3D_RING_OFFSET) / 4UL;
    listPhys = osmgaMmapCmdPhysical + OSMGA_HW3D_RING_OFFSET;

    r = osmgaMapUncachedBlock(frameBufferPhysical, lo, hi, &aWin, &lWin, &win);
    if (r != IO_R_SUCCESS) {
        IOLog("OpenStepMGA M1-2c: could not map the canary window (%d)\n",
              (int)r);
        return;
    }
    if (!osmgaStormWaitIdle(base)) {
        IOLog("OpenStepMGA M1-2c: engine BUSY at entry\n");
        goto unmap;
    }

    /* Canary the FULL row width, not just the drawn columns: an escape in x
     * lands further along the same row, which a narrow check would miss. */
    for (row = 0UL; row < winRows; row++)
        for (col = 0UL; col < stride; col++)
            win[row * stride + col] = OSMGA_M1C_CANARY;

    lim.pitchBytes  = (unsigned long)di->rowBytes;
    lim.clipY1      = drawRows - 1UL;
    lim.clipX1      = OSMGA_S1_W - 1UL;
    lim.colourStart = OSMGA_S1_VRAM_BLOCK;
    lim.colourEnd   = OSMGA_S1_VRAM_PROVEN;
    lim.depthStart  = OSMGA_S1_VRAM_BLOCK;
    lim.depthEnd    = OSMGA_S1_VRAM_PROVEN;
    lim.texStart    = OSMGA_S1_VRAM_BLOCK;
    lim.texEnd      = OSMGA_S1_VRAM_PROVEN;
    lim.batchBytes  = OSMGA_HW3D_BATCH_BYTES;
    lim.maxEdgeWalk = OSMGA_HW3D_EDGE_WALK;


    for (b = 0UL; b < bands; b++) {
        unsigned long y0 = b * band;
        long dxL = (b == 1UL || b == 3UL || b == 4UL) ? -OSMGA_M1C_SLOPE : 0L;
        long dxR = (b == 2UL || b == 3UL || b == 5UL) ?  OSMGA_M1C_SLOPE : 0L;
        long left0  = (b == 4UL) ? 32L : 0L;
        long right0 = (b == 5UL) ? 31L : (long)(OSMGA_S1_W - 1UL);

        for (col = 0UL; col < sizeof(OSMGAHW3DBatch) / 4UL; col++)
            ((unsigned long *)batch)[col] = 0UL;
        batch->magic = OSMGA_HW3D_MAGIC;
        batch->version = OSMGA_HW3D_VERSION;
        batch->triCount = 1UL;
        batch->state.dstorg = OSMGA_M1C_DSTORG;
        /*
         * The geometry the validator now requires.  It has to be set AFTER
         * the batch is zeroed, not before: putting it above the clearing
         * loop compiles, changes nothing, and leaves every batch here
         * failing on the pitch before it reaches what the test is for.
         */
        batch->state.dstPitch  = stride;
        batch->state.dstWidth  = lim.clipX1 + 1UL;
        batch->state.dstHeight = lim.clipY1 + 1UL;
        osmgaM1cTri(&batch->tri[0], y0, band,
                    left0, dxL, (long)band, right0, dxR, (long)band);

        v = osmgaHW3DValidate(batch, &lim, &badTri);
        if (v != OSMGA_HW3D_OK) {
            IOLog("OpenStepMGA M1-2c: band %lu refused (%d) -- the hostile "
                  "edge must PASS validation for this test to mean "
                  "anything\n", b, v);
            goto unmap;
        }
        total = osmgaHW3DEncode(list, listDwords, batch, &tail);
        if (total == 0UL) {
            IOLog("OpenStepMGA M1-2c: encoding failed\n");
            goto unmap;
        }

        if (!osmgaStormWaitFifo(base, 13U)) goto unmap;
        osmgaStormInitState(base, stride, 0UL, OSMGA_S1_W - 1UL,
                            y0 * stride, (y0 + band - 1UL) * stride);
        osmgaW32(base, MGA_ICLEAR, MGA_SOFTRAPICLR);
        for (spins = 0UL; spins < OSMGA_S1_SPIN_LIMIT; spins++) {
            status = osmgaR32(base, MGA_ENGSTATUS) & MGA_DMA_DONE_MASK;
            if (status == MGA_STATUS_ENDPRDMASTS) break;
        }
        if (spins >= OSMGA_S1_SPIN_LIMIT) {
            IOLog("OpenStepMGA M1-2c: DMA not quiescent before band %lu\n", b);
            goto unmap;
        }
        {   unsigned long sum = 0UL, i;

            for (i = 0UL; i < total; i++) sum += list[i];
            (void)osmgaR32(base, MGA_ENGSTATUS);
            if (sum == 0xFFFFFFFFUL) IOLog("barrier %lu\n", sum);
        }
        osmgaW32(base, MGA_PRIMADDRESS, listPhys | MGA_DMA_GENERAL);
        osmgaW32(base, MGA_PRIMEND, (listPhys + tail * 4UL) | MGA_DMA_GENERAL);
        for (spins = 0UL; spins < OSMGA_S1_SPIN_LIMIT; spins++) {
            status = osmgaR32(base, MGA_ENGSTATUS);
            if ((status & MGA_DMA_DONE_MASK) == MGA_DMA_DONE_VALUE) break;
        }
        osmgaW32(base, MGA_ICLEAR, MGA_SOFTRAPICLR);
        if (spins >= OSMGA_S1_SPIN_LIMIT) {
            IOLog("OpenStepMGA M1-2c: band %lu did not complete (%08lx)\n",
                  b, status);
            goto unmap;
        }
        if (!osmgaStormWaitIdle(base)) goto unmap;

        inside[b] = 0UL;
        row0[b] = 0UL;
        for (row = y0; row < y0 + band; row++)
            for (col = 0UL; col < OSMGA_S1_W; col++)
                if (win[(dstRow + row) * stride + col] != OSMGA_M1C_CANARY) {
                    inside[b]++;
                    if (row == y0) row0[b]++;
                }
    }

    for (row = 0UL; row < winRows; row++)
        for (col = 0UL; col < stride; col++) {
            if (row >= dstRow && row < dstRow + drawRows &&
                col < OSMGA_S1_W)
                continue;                       /* the intended rectangle */
            if (win[row * stride + col] != OSMGA_M1C_CANARY) {
                if (firstBad == 0xFFFFFFFFUL)
                    firstBad = row * 10000UL + col;
                outside++;
            }
        }

    IOLog("OpenStepMGA M1-2c: edge displacement %ld over the band "
          "(not per row); drew inside the clip "
          "%lu %lu %lu %lu %lu %lu of %lu each\n",
          OSMGA_M1C_SLOPE, inside[0], inside[1], inside[2], inside[3],
          inside[4], inside[5], band * OSMGA_S1_W);
    IOLog("OpenStepMGA M1-2c: bands 4,5 start inside and walk out -- drew "
          "%lu and %lu (engaged wants 1248, ignored would be 640); their "
          "first rows %lu and %lu (want 32)\n",
          inside[4], inside[5], row0[4], row0[5]);
    IOLog("OpenStepMGA M1-2c: canary %lu rows x %lu cols; words changed "
          "outside the intended rectangle: %lu\n",
          winRows, stride, outside);
    if (outside != 0UL)
        IOLog("OpenStepMGA M1-2c: first escape at window row %lu col %lu\n",
              firstBad / 10000UL, firstBad % 10000UL);

    if (outside != 0UL)
        IOLog("OpenStepMGA M1-2c: STOP -- a hostile edge escaped the clip. "
              "Containment cannot rest on clipping and the validator must "
              "bound the AR values itself\n");
    else if (inside[0] == 0UL)
        IOLog("OpenStepMGA M1-2c: FAIL -- the benign control drew nothing, "
              "so the hostile bands prove nothing\n");
    else if (row0[4] != 32UL || row0[5] != 32UL)
        IOLog("OpenStepMGA M1-2c: FAIL -- the edges that start inside did "
              "not narrow the first row, so the AR walk may not have run "
              "and the hostile bands prove nothing\n");
    else
        IOLog("OpenStepMGA M1-2c: PASS -- edges that walk far outside the "
              "clip wrote nothing outside it\n");

unmap:
    if (aWin != 0) (void)IOUnmapPhysicalFromIOTask(aWin, lWin);
}

@end
