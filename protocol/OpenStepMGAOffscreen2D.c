/* Pure-C offscreen-only 2D admission; no target interfaces. */

#include "OpenStepMGAOffscreen2D.h"
#include "OpenStepMGAOffscreenAllocator.h"

static int
validate_transaction_and_surface(const OSMGAModeTransaction *transaction,
                                 const OSMGAOffscreenAllocator *allocator,
                                 const OSMGAOffscreenSurface *surface,
                                 OSMGAOffscreen2DReason *reason)
{
    OSMGAOffscreenAllocatorReason ownership_reason;

    if (transaction->state != OSMGA_MODE_TRANSACTION_LINEAR_ACTIVE) {
        *reason = OSMGA_OFFSCREEN_2D_TRANSACTION_NOT_ACTIVE;
        return 0;
    }
    if (surface->surface_id == 0) {
        *reason = OSMGA_OFFSCREEN_2D_SURFACE_ID_INVALID;
        return 0;
    }
    if (!surface->kernel_allocation_verified) {
        *reason = OSMGA_OFFSCREEN_2D_ALLOCATION_UNVERIFIED;
        return 0;
    }
    if (!surface->outside_scanout_verified) {
        *reason = OSMGA_OFFSCREEN_2D_SCANOUT_SEPARATION_UNVERIFIED;
        return 0;
    }
    if (!OSMGAValidateLiveOffscreenSurface(allocator, surface,
                                           &ownership_reason)) {
        *reason = OSMGA_OFFSCREEN_2D_SURFACE_NOT_LIVE;
        return 0;
    }
    return 1;
}

int
OSMGAValidateOffscreenClear32(const OSMGAModeTransaction *transaction,
                              const OSMGAOffscreenAllocator *allocator,
                              const OSMGAOffscreenSurface *surface,
                              unsigned int x, unsigned int y,
                              unsigned int width, unsigned int height,
                              OSMGAOffscreen2DReason *reason,
                              OSMGACommandValidation *validation)
{
    if (reason == 0 || validation == 0) {
        return 0;
    }
    if (transaction == 0 || allocator == 0 || surface == 0) {
        *reason = OSMGA_OFFSCREEN_2D_INVALID_ARGUMENT;
        return 0;
    }
    if (!validate_transaction_and_surface(transaction, allocator, surface,
                                          reason)) {
        return 0;
    }
    if (!OSMGAValidateClear32(&surface->geometry, x, y, width, height,
                              validation)) {
        *reason = OSMGA_OFFSCREEN_2D_COMMAND_INVALID;
        return 0;
    }
    *reason = OSMGA_OFFSCREEN_2D_OK;
    return 1;
}

int
OSMGAValidateOffscreenCopy32(const OSMGAModeTransaction *transaction,
                             const OSMGAOffscreenAllocator *allocator,
                             const OSMGAOffscreenSurface *source,
                             unsigned int source_x, unsigned int source_y,
                             const OSMGAOffscreenSurface *destination,
                             unsigned int destination_x,
                             unsigned int destination_y,
                             unsigned int width, unsigned int height,
                             OSMGAOffscreen2DReason *reason,
                             OSMGACommandValidation *validation)
{
    if (reason == 0 || validation == 0) {
        return 0;
    }
    if (transaction == 0 || allocator == 0 || source == 0 || destination == 0) {
        *reason = OSMGA_OFFSCREEN_2D_INVALID_ARGUMENT;
        return 0;
    }
    if (!validate_transaction_and_surface(transaction, allocator, source, reason) ||
        !validate_transaction_and_surface(transaction, allocator, destination,
                                          reason)) {
        return 0;
    }
    if (source->surface_id == destination->surface_id) {
        *reason = OSMGA_OFFSCREEN_2D_SELF_COPY_UNSUPPORTED;
        return 0;
    }
    if (!OSMGAValidateClear32(&source->geometry, source_x, source_y,
                              width, height, validation) ||
        !OSMGAValidateClear32(&destination->geometry, destination_x,
                              destination_y, width, height, validation)) {
        *reason = OSMGA_OFFSCREEN_2D_COMMAND_INVALID;
        return 0;
    }
    *reason = OSMGA_OFFSCREEN_2D_OK;
    return 1;
}

const char *
OSMGAOffscreen2DReasonString(OSMGAOffscreen2DReason reason)
{
    switch (reason) {
    case OSMGA_OFFSCREEN_2D_OK: return "ok";
    case OSMGA_OFFSCREEN_2D_INVALID_ARGUMENT: return "invalid-argument";
    case OSMGA_OFFSCREEN_2D_TRANSACTION_NOT_ACTIVE: return "transaction-not-active";
    case OSMGA_OFFSCREEN_2D_SURFACE_ID_INVALID: return "surface-id-invalid";
    case OSMGA_OFFSCREEN_2D_ALLOCATION_UNVERIFIED: return "allocation-unverified";
    case OSMGA_OFFSCREEN_2D_SCANOUT_SEPARATION_UNVERIFIED: return "scanout-separation-unverified";
    case OSMGA_OFFSCREEN_2D_SURFACE_NOT_LIVE: return "surface-not-live";
    case OSMGA_OFFSCREEN_2D_COMMAND_INVALID: return "command-invalid";
    case OSMGA_OFFSCREEN_2D_SELF_COPY_UNSUPPORTED: return "self-copy-unsupported";
    }
    return "unknown";
}
