/*
 * R6 replacement display driver for Matrox MGA G450 (primary head).
 *
 * Programs a full X.Org-derived G450 32bpp linear mode (generic VGA + RAMDAC
 * pixel format + G450 pixel-PLL lock search) in enterLinearMode; init maps the
 * framebuffer (BAR0, mapMemoryRange range 0) and the MMIO control aperture
 * (BAR1, via IOMapPhysicalIntoIOTask) and publishes the fixed 1600x1200x32
 * IODisplayInfo.  Hardware register values are the X.Org-verified constants in
 * docs/R6_G450_FULL_LINEAR_MODESET.md; the reviewed mode transaction wraps them
 * for input validation, PLL-lock sequencing, and rollback reporting.
 *
 * Owns the display only when selected in System.config "Active Drivers" and
 * cold-rebooted.  Building does not change the running display.
 */

#ifndef OPENSTEP_MGA_REPLACEMENT_DISPLAY_H
#define OPENSTEP_MGA_REPLACEMENT_DISPLAY_H

#import <driverkit/IOFrameBufferDisplay.h>

/* mapMemoryRange: indices, matching Default.table "Memory Maps" order. */
#define OSMGA_RANGE_FRAMEBUFFER 0
#define OSMGA_RANGE_VGA         1
#define OSMGA_RANGE_BIOS        2

@interface OpenStepMGAReplacementDisplay : IOFrameBufferDisplay
{
    /* manual configuration */
    BOOL manualVideoMemoryConfigured;
    unsigned int configuredVideoMemoryBytes;

    /* chip classification (from PCI revision) */
    unsigned int chipVendorDevice;
    unsigned int chipRevision;
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

    /* lifecycle */
    BOOL linearModeActive;
}

+ (BOOL)probe:deviceDescription;
- initFromDeviceDescription:deviceDescription;
- (BOOL)readManualMemoryConfiguration:configTable;
- (void)selectModeFromConfig:configTable;
- setTransferTable:(const unsigned int *)table count:(int)count;
- (void)enterLinearMode;
- (void)revertToVGAMode;
- (unsigned int)displayMemorySize;
- (unsigned int)ramdacSpeed;
- setBrightness:(int)level token:(int)token;
- free;

@end

#endif /* OPENSTEP_MGA_REPLACEMENT_DISPLAY_H */
