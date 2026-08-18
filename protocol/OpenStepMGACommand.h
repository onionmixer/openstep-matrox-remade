/*
 * Pure-C P3 command-envelope validation.
 *
 * These types contain surface geometry only.  They intentionally contain no
 * virtual address, physical address, BAR offset, VM handle, or device state.
 */

#ifndef OPENSTEP_MGA_COMMAND_H
#define OPENSTEP_MGA_COMMAND_H

typedef struct {
    unsigned int width;
    unsigned int height;
    unsigned int stride_pixels;
    unsigned long allocation_bytes;
} OSMGASurfaceGeometry;

typedef enum {
    OSMGA_COMMAND_VALID = 0,
    OSMGA_COMMAND_INVALID_ARGUMENT,
    OSMGA_COMMAND_INVALID_SURFACE,
    OSMGA_COMMAND_SURFACE_OVERFLOW,
    OSMGA_COMMAND_SURFACE_TOO_SMALL,
    OSMGA_COMMAND_RECT_OUT_OF_RANGE,
    OSMGA_COMMAND_TRIANGLE_OUT_OF_RANGE,
    OSMGA_COMMAND_TRIANGLE_DEGENERATE
} OSMGACommandValidation;

/* Validate a 32-bit-pixel surface and return its required allocation bytes. */
int OSMGAValidateSurface32(const OSMGASurfaceGeometry *surface,
                           unsigned long *required_bytes,
                           OSMGACommandValidation *validation);

/* Validate an unclipped clear rectangle wholly within a validated surface. */
int OSMGAValidateClear32(const OSMGASurfaceGeometry *surface,
                         unsigned int x, unsigned int y,
                         unsigned int width, unsigned int height,
                         OSMGACommandValidation *validation);

/* Validate an unclipped integer-coordinate flat triangle. */
int OSMGAValidateTriangle32(const OSMGASurfaceGeometry *surface,
                            int x0, int y0, int x1, int y1, int x2, int y2,
                            OSMGACommandValidation *validation);

#endif /* OPENSTEP_MGA_COMMAND_H */
