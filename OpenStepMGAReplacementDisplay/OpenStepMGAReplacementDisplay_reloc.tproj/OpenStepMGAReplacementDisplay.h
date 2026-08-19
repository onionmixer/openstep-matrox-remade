/*
 * R6 replacement display driver for Matrox MGA G450 (primary head).
 *
 * Programs a full X.Org-derived G450 linear mode directly in enterLinearMode
 * (generic VGA + depth-specific RAMDAC pixel format/palette + G450 pixel-PLL
 * candidate search, no mode-validation/transaction layer); init maps the
 * framebuffer (BAR0, via mapFrameBufferAtPhysicalAddress:length:) and the MMIO control aperture
 * (BAR1, via IOMapPhysicalIntoIOTask), selects a resolution+pixel-format from
 * the config-table "Display Mode" string, and publishes the matching
 * IODisplayInfo.  Hardware register values are the X.Org-verified constants
 * in docs/R6_G450_FULL_LINEAR_MODESET.md; the PLL search is a faithful port
 * of the original documented in docs/R6_G450_PIXEL_PLL_ALGORITHM.md.
 *
 * Owns the display only when selected in System.config "Active Drivers" and
 * cold-rebooted.  Building does not change the running display.
 */

#ifndef OPENSTEP_MGA_REPLACEMENT_DISPLAY_H
#define OPENSTEP_MGA_REPLACEMENT_DISPLAY_H

#import <driverkit/IOFrameBufferDisplay.h>
#import <mach/machine/simple_lock.h>

@interface OpenStepMGAReplacementDisplay : IOFrameBufferDisplay
{
    /* manual configuration ("MGA Memory Size") */
    unsigned int configuredVideoMemoryBytes;

    /* chip classification (from PCI revision) */
    BOOL chipIsG450;

    /* mapping state */
    BOOL frameBufferMapped;
    BOOL mmioMapped;
    vm_address_t mmioBase;
    unsigned long mmioPhysical;
    unsigned long frameBufferPhysical;

    /* selected display mode: resolution index + pixel-format index */
    int selectedResIndex;
    int selectedFormatIndex;

    /* window-server colormap for 8bpp PseudoColor (setTransferTable:count:) */
    unsigned char paletteRed[256];
    unsigned char paletteGreen[256];
    unsigned char paletteBlue[256];
    BOOL paletteValid;

    /* opt-in S1 Storm 2D engine liveness test ("Storm 2D Test" = "Yes").
     * Default NO: a normal boot never writes a drawing-engine register. */
    BOOL stormTestEnabled;

    /* opt-in D1 primary-DMA ring test ("DMA Ring Test" = "Yes").
     * Default NO: D1-0 only allocates, but it consumes 64 KiB of the
     * conventional-memory arena that ISA DMA users share. */
    BOOL dmaRingTestEnabled;

    /* opt-in D2 WARP configuration probe ("WARP Test" = "Yes"). */
    BOOL warpTestEnabled;

    /* opt-in D3 interpolating-rasteriser probe ("Raster Test" = "Yes"). */
    BOOL rasterTestEnabled;

    /* S3 IODisplayDoBlit acceleration.
     *   stormBlitReady   -- accept IODisplayDoBlit requests at all
     *   stormBlitFailed  -- an execute timed out; the engine may still be
     *                       writing, so acceleration is permanently off and
     *                       every later request is refused
     *   stormBusy        -- one blit transaction in flight; a concurrent
     *                       caller is refused (IO_R_RESOURCE) rather than
     *                       queued, so no thread ever waits on another
     *   stormLock        -- guards stormBusy only, held for a few instructions
     */
    BOOL stormBlitReady;
    BOOL stormBlitFailed;
    BOOL stormBusy;
    simple_lock_data_t stormLock;

    /*
     * S3b-prep telemetry (docs/S3B_PREP_INSTRUMENTATION_PLAN.md).  Read out
     * through getIntValues:forParameter:"OSMGAStats".  Deliberately updated
     * without a lock: cursor entry points may run at interrupt context, so
     * they must not take locks, allocate, log or wait.  Lost or slightly
     * inconsistent counts are acceptable -- we want to know whether something
     * happens and roughly how often, not exact totals.
     */
    volatile unsigned statBlitRequests;   /* RPC boundary only, not self-test */
    volatile unsigned statBlitOk;
    volatile unsigned statBlitNoop;
    volatile unsigned statRefusedDisabled;
    volatile unsigned statRefusedGeometry;
    volatile unsigned statRefusedBusy;
    volatile unsigned statRefusedPreExec;
    volatile unsigned statPostExecTimeout;
    volatile unsigned statCursorShow;
    volatile unsigned statCursorMove;
    volatile unsigned statCursorHide;
    volatile unsigned statCursorWhileBusy; /* cursor entered mid-transaction */
    volatile unsigned statThin1px;
    volatile unsigned statEnterLinear;
    volatile unsigned statRevertVGA;
    volatile unsigned statTransferTable;  /* setTransferTable: calls */


    /* lifecycle */
    BOOL linearModeActive;
}

+ (BOOL)probe:deviceDescription;
- initFromDeviceDescription:deviceDescription;
- (BOOL)readManualMemoryConfiguration:configTable;
- (void)selectModeFromConfig:configTable;
- (void)runStormLivenessTest;
- (void)runStormBlitTest;
- (void)runStormBlitApiTest;
- (void)runDmaRingAllocTest;
- (void)runDmaRingBuildTest;
- (void)runDmaRingStartTest;
- (void)runWarpConfigTest;
- (void)runWarpUcodePlacementTest;
- (BOOL)runWarpPipeOnce:(unsigned long)startPhys
                  label:(const char *)label
               ringVirt:(unsigned long *)ring
               ringPhys:(unsigned)ringPhys;
- (void)runWarpPipeStartTest;
- (void)runRasterInterpolationTest;
- (void)runDstorgOriginTest;
- (void)runDepthCompareTest;
- (void)runDepthIsolationTest;
- (void)runDepthEncodingTest;
- (void)runTextureIdentityTest;
- (void)runTextureSlopeTest;
- (void)runTextureOriginTest;
- (BOOL)stormBlitCheckSrcX:(unsigned)srcX srcY:(unsigned)srcY
                     width:(unsigned)w height:(unsigned)h
                      dstX:(unsigned)dstX dstY:(unsigned)dstY
                     label:(const char *)label;
- (IOReturn)doDisplayBlitSrcX:(unsigned)srcX srcY:(unsigned)srcY
                        width:(unsigned)w height:(unsigned)h
                         dstX:(unsigned)dstX dstY:(unsigned)dstY
                       reason:(unsigned *)outReason;
- (IOReturn)rpcBlitFrom:(unsigned *)p;
- hideCursor:(int)token;
- moveCursor:(Point *)cursorLoc frame:(int)frame token:(int)t;
- showCursor:(Point *)cursorLoc frame:(int)frame token:(int)t;
- (IOReturn)getIntValues:(unsigned *)parameterArray
            forParameter:(IOParameterName)parameterName
                   count:(unsigned *)count;
- (IOReturn)setIntValues:(unsigned *)parameterArray
            forParameter:(IOParameterName)parameterName
                   count:(unsigned)count;
- (BOOL)runStormBlitOnceFrom:(unsigned long)srcY
                         toX:(unsigned long)dstX
                         toY:(unsigned long)dstY
                      stride:(unsigned long)stridePixels
                       label:(const char *)label;
- setTransferTable:(const unsigned int *)table count:(int)count;
- (void)enterLinearMode;
- (void)revertToVGAMode;
- (unsigned int)displayMemorySize;
- (unsigned int)ramdacSpeed;
- setBrightness:(int)level token:(int)token;
- free;

@end

#endif /* OPENSTEP_MGA_REPLACEMENT_DISPLAY_H */
