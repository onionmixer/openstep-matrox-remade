/*
 * Pure-C reference oracle for future P3 readback tests.
 *
 * This module operates only on caller-owned ordinary memory.  It does not
 * know PCI, VRAM, framebuffers, MGA registers, or DriverKit.
 */

#ifndef OPENSTEP_MGA_REFERENCE_H
#define OPENSTEP_MGA_REFERENCE_H

#define OSMGA_REFERENCE_U32_MASK 4294967295UL
#define OSMGA_REFERENCE_COORDINATE_LIMIT 4095

/* Clear only width pixels in each stride_pixels row; padding is preserved. */
int OSMGAReferenceClear32(unsigned long *pixels,
                          unsigned int width,
                          unsigned int height,
                          unsigned int stride_pixels,
                          unsigned long pixel);

/* Clear one in-bounds active rectangle; padding remains preserved. */
int OSMGAReferenceClearRect32(unsigned long *pixels,
                              unsigned int surface_width,
                              unsigned int surface_height,
                              unsigned int stride_pixels,
                              unsigned int x, unsigned int y,
                              unsigned int width, unsigned int height,
                              unsigned long pixel);

/* Copy between distinct caller-owned surfaces; self-copy is intentionally off. */
int OSMGAReferenceCopyRect32(const unsigned long *source,
                             unsigned int source_width,
                             unsigned int source_height,
                             unsigned int source_stride_pixels,
                             unsigned int source_x, unsigned int source_y,
                             unsigned long *destination,
                             unsigned int destination_width,
                             unsigned int destination_height,
                             unsigned int destination_stride_pixels,
                             unsigned int destination_x,
                             unsigned int destination_y,
                             unsigned int width, unsigned int height);

/*
 * Scale a complete active source surface to a complete distinct destination
 * surface with deterministic nearest-neighbor sampling.  Destination padding
 * is preserved.  This is a CPU reference/presentation oracle, not a hardware
 * stretch-blit implementation.
 */
int OSMGAReferenceScaleNearest32(const unsigned long *source,
                                 unsigned int source_width,
                                 unsigned int source_height,
                                 unsigned int source_stride_pixels,
                                 unsigned long *destination,
                                 unsigned int destination_width,
                                 unsigned int destination_height,
                                 unsigned int destination_stride_pixels);

/* Deterministic byte-order-defined FNV-1a checksum for active pixels only. */
int OSMGAReferenceChecksum32(const unsigned long *pixels,
                             unsigned int width,
                             unsigned int height,
                             unsigned int stride_pixels,
                             unsigned long *checksum);

/* Compare active pixels only and report the first row-major mismatch index. */
int OSMGAReferenceCompare32(const unsigned long *expected,
                            const unsigned long *actual,
                            unsigned int width,
                            unsigned int height,
                            unsigned int stride_pixels,
                            unsigned long *mismatch_index);

/*
 * Fill a clockwise or counter-clockwise integer-coordinate triangle using
 * pixel-center sampling.  Coordinates and dimensions are intentionally
 * limited to +/-4095/4095 so all edge calculations remain signed-32-bit
 * safe on OPENSTEP i386.  Padding is preserved.
 */
int OSMGAReferenceFillTriangle32(unsigned long *pixels,
                                 unsigned int width,
                                 unsigned int height,
                                 unsigned int stride_pixels,
                                 int x0, int y0, int x1, int y1,
                                 int x2, int y2,
                                 unsigned long pixel);

typedef enum {
    OSMGA_REFERENCE_DEPTH_NEVER = 0,
    OSMGA_REFERENCE_DEPTH_LESS,
    OSMGA_REFERENCE_DEPTH_LEQUAL,
    OSMGA_REFERENCE_DEPTH_ALWAYS
} OSMGAReferenceDepthFunction;

/*
 * Apply one 16-bit depth comparison at an active pixel.  A valid call returns
 * 1 and writes passed as 0/1.  Color changes only on pass; depth changes only
 * when both pass and write_depth are nonzero.
 */
int OSMGAReferenceDepthTestWrite16(unsigned long *pixels,
                                   unsigned short *depth,
                                   unsigned int width,
                                   unsigned int height,
                                   unsigned int stride_pixels,
                                   int x, int y,
                                   unsigned short incoming_depth,
                                   unsigned long pixel,
                                   OSMGAReferenceDepthFunction function,
                                   int write_depth,
                                   int *passed);

/* Non-premultiplied AARRGGBB SRC_ALPHA / ONE_MINUS_SRC_ALPHA reference. */
unsigned long OSMGAReferenceBlendSrcAlpha32(unsigned long source,
                                            unsigned long destination);

/*
 * Sample a caller-owned AARRGGBB 2D texture with unsigned 16.16 coordinates,
 * nearest filtering, and explicit clamp-to-edge behavior.  It is a minimal
 * P3 texture-readback oracle, not a complete Mesa texture environment.
 */
int OSMGAReferenceSampleTextureNearestClamp32(const unsigned long *texels,
                                              unsigned int width,
                                              unsigned int height,
                                              unsigned long u_16_16,
                                              unsigned long v_16_16,
                                              unsigned long *pixel);

#endif /* OPENSTEP_MGA_REFERENCE_H */
