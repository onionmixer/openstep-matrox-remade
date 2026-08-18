/*
 * OpenStepMGAOffscreen2D.h - geometry-only admission for first 2D requests.
 *
 * Surfaces have an opaque nonzero ID and geometry. No address, register,
 * queue descriptor, or target device handle is represented here.
 */

#ifndef OPENSTEP_MGA_OFFSCREEN_2D_H
#define OPENSTEP_MGA_OFFSCREEN_2D_H

#include "OpenStepMGACommand.h"
#include "OpenStepMGAModeTransaction.h"

struct OSMGAOffscreenAllocator;

typedef struct {
    unsigned long surface_id;
    OSMGASurfaceGeometry geometry;
    int kernel_allocation_verified;
    int outside_scanout_verified;
} OSMGAOffscreenSurface;

typedef enum {
    OSMGA_OFFSCREEN_2D_OK = 0,
    OSMGA_OFFSCREEN_2D_INVALID_ARGUMENT,
    OSMGA_OFFSCREEN_2D_TRANSACTION_NOT_ACTIVE,
    OSMGA_OFFSCREEN_2D_SURFACE_ID_INVALID,
    OSMGA_OFFSCREEN_2D_ALLOCATION_UNVERIFIED,
    OSMGA_OFFSCREEN_2D_SCANOUT_SEPARATION_UNVERIFIED,
    OSMGA_OFFSCREEN_2D_SURFACE_NOT_LIVE,
    OSMGA_OFFSCREEN_2D_COMMAND_INVALID,
    OSMGA_OFFSCREEN_2D_SELF_COPY_UNSUPPORTED
} OSMGAOffscreen2DReason;

int OSMGAValidateOffscreenClear32(const OSMGAModeTransaction *transaction,
                                  const struct OSMGAOffscreenAllocator *allocator,
                                  const OSMGAOffscreenSurface *surface,
                                  unsigned int x, unsigned int y,
                                  unsigned int width, unsigned int height,
                                  OSMGAOffscreen2DReason *reason,
                                  OSMGACommandValidation *validation);

int OSMGAValidateOffscreenCopy32(const OSMGAModeTransaction *transaction,
                                 const struct OSMGAOffscreenAllocator *allocator,
                                 const OSMGAOffscreenSurface *source,
                                 unsigned int source_x, unsigned int source_y,
                                 const OSMGAOffscreenSurface *destination,
                                 unsigned int destination_x,
                                 unsigned int destination_y,
                                 unsigned int width, unsigned int height,
                                 OSMGAOffscreen2DReason *reason,
                                 OSMGACommandValidation *validation);

const char *OSMGAOffscreen2DReasonString(OSMGAOffscreen2DReason reason);

#endif /* OPENSTEP_MGA_OFFSCREEN_2D_H */
