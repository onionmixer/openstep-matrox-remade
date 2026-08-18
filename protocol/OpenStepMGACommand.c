#include "OpenStepMGACommand.h"

#define OSMGA_COMMAND_U32_MAX 4294967295UL

static int
set_validation(OSMGACommandValidation *validation, OSMGACommandValidation value)
{
    if (validation == 0) {
        return 0;
    }
    *validation = value;
    return value == OSMGA_COMMAND_VALID;
}

int
OSMGAValidateSurface32(const OSMGASurfaceGeometry *surface,
                       unsigned long *required_bytes,
                       OSMGACommandValidation *validation)
{
    unsigned long row_bytes;
    unsigned long total_bytes;

    if (surface == 0 || required_bytes == 0 || validation == 0) {
        return 0;
    }
    if (surface->width == 0 || surface->height == 0 ||
        surface->stride_pixels < surface->width ||
        surface->allocation_bytes == 0) {
        return set_validation(validation, OSMGA_COMMAND_INVALID_SURFACE);
    }
    if ((unsigned long)surface->stride_pixels > OSMGA_COMMAND_U32_MAX / 4UL) {
        return set_validation(validation, OSMGA_COMMAND_SURFACE_OVERFLOW);
    }
    row_bytes = (unsigned long)surface->stride_pixels * 4UL;
    if ((unsigned long)surface->height > OSMGA_COMMAND_U32_MAX / row_bytes) {
        return set_validation(validation, OSMGA_COMMAND_SURFACE_OVERFLOW);
    }
    total_bytes = row_bytes * (unsigned long)surface->height;
    if (total_bytes > surface->allocation_bytes) {
        return set_validation(validation, OSMGA_COMMAND_SURFACE_TOO_SMALL);
    }
    *required_bytes = total_bytes;
    return set_validation(validation, OSMGA_COMMAND_VALID);
}

int
OSMGAValidateClear32(const OSMGASurfaceGeometry *surface,
                     unsigned int x, unsigned int y,
                     unsigned int width, unsigned int height,
                     OSMGACommandValidation *validation)
{
    unsigned long required_bytes;

    if (validation == 0) {
        return 0;
    }
    if (!OSMGAValidateSurface32(surface, &required_bytes, validation)) {
        return 0;
    }
    if (width == 0 || height == 0 || x >= surface->width || y >= surface->height ||
        width > surface->width - x || height > surface->height - y) {
        return set_validation(validation, OSMGA_COMMAND_RECT_OUT_OF_RANGE);
    }
    return set_validation(validation, OSMGA_COMMAND_VALID);
}

static int
inside_surface(const OSMGASurfaceGeometry *surface, int x, int y)
{
    return x >= 0 && y >= 0 && x < (int)surface->width &&
           y < (int)surface->height;
}

int
OSMGAValidateTriangle32(const OSMGASurfaceGeometry *surface,
                        int x0, int y0, int x1, int y1, int x2, int y2,
                        OSMGACommandValidation *validation)
{
    unsigned long required_bytes;
    long area;

    if (validation == 0) {
        return 0;
    }
    if (!OSMGAValidateSurface32(surface, &required_bytes, validation)) {
        return 0;
    }
    if (surface->width > 4095U || surface->height > 4095U ||
        !inside_surface(surface, x0, y0) || !inside_surface(surface, x1, y1) ||
        !inside_surface(surface, x2, y2)) {
        return set_validation(validation, OSMGA_COMMAND_TRIANGLE_OUT_OF_RANGE);
    }
    area = ((long)x1 - (long)x0) * ((long)y2 - (long)y0) -
           ((long)y1 - (long)y0) * ((long)x2 - (long)x0);
    if (area == 0) {
        return set_validation(validation, OSMGA_COMMAND_TRIANGLE_DEGENERATE);
    }
    return set_validation(validation, OSMGA_COMMAND_VALID);
}
