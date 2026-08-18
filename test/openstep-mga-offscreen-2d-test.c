#include <stdio.h>

#include "OpenStepMGAOffscreenAllocator.h"

static int failures;

static void
expect(int condition, const char *name)
{
    if (!condition) {
        printf("OPENSTEP_MGA_OFFSCREEN_2D_TEST=fail:%s\n", name);
        failures++;
    }
}

static void
make_surface_request(OSMGAOffscreenSurfaceRequest *request)
{
    request->width = 64;
    request->height = 32;
    request->stride_pixels = 64;
}

int
main(void)
{
    OSMGAModeTransaction transaction;
    OSMGAOffscreenArena arena;
    OSMGAOffscreenAllocator allocator;
    OSMGAOffscreenSurfaceRequest request;
    OSMGAOffscreenSurface source;
    OSMGAOffscreenSurface destination;
    OSMGAOffscreen2DReason reason;
    OSMGAOffscreenAllocatorReason allocator_reason;
    OSMGACommandValidation validation;

    arena.arena_bytes = 16384UL;
    arena.alignment_bytes = 4096UL;
    arena.outside_scanout_region_verified = 1;
    expect(OSMGAInitializeOffscreenAllocator(&arena, &allocator,
                                              &allocator_reason) == 1,
           "allocator-init");
    make_surface_request(&request);
    expect(OSMGAAllocateOffscreenSurface(&allocator, &request, &source,
                                         &allocator_reason) == 1,
           "source-allocation");
    expect(OSMGAAllocateOffscreenSurface(&allocator, &request, &destination,
                                         &allocator_reason) == 1,
           "destination-allocation");

    transaction.state = OSMGA_MODE_TRANSACTION_PREFLIGHT_READY;
    expect(OSMGAValidateOffscreenClear32(&transaction, &allocator, &source,
                                         0, 0, 1, 1,
                                         &reason, &validation) == 0,
           "active-transaction-required");
    expect(reason == OSMGA_OFFSCREEN_2D_TRANSACTION_NOT_ACTIVE,
           "active-transaction-reason");

    transaction.state = OSMGA_MODE_TRANSACTION_LINEAR_ACTIVE;
    source.outside_scanout_verified = 0;
    expect(OSMGAValidateOffscreenClear32(&transaction, &allocator, &source,
                                         0, 0, 1, 1,
                                         &reason, &validation) == 0,
           "scanout-separation-required");
    expect(reason == OSMGA_OFFSCREEN_2D_SCANOUT_SEPARATION_UNVERIFIED,
           "scanout-separation-reason");

    source = allocator.active_surfaces[0];
    source.geometry.width = 63;
    expect(OSMGAValidateOffscreenClear32(&transaction, &allocator, &source,
                                         0, 0, 1, 1,
                                         &reason, &validation) == 0,
           "mutated-surface-rejected");
    expect(reason == OSMGA_OFFSCREEN_2D_SURFACE_NOT_LIVE,
           "mutated-surface-reason");
    source = allocator.active_surfaces[0];
    expect(OSMGAValidateOffscreenClear32(&transaction, &allocator, &source,
                                         0, 0, 64, 32,
                                         &reason, &validation) == 1,
           "full-offscreen-clear");
    expect(reason == OSMGA_OFFSCREEN_2D_OK &&
           validation == OSMGA_COMMAND_VALID, "clear-reason");
    expect(OSMGAValidateOffscreenClear32(&transaction, &allocator, &source,
                                         63, 0, 2, 1,
                                         &reason, &validation) == 0,
           "clear-range-enforced");
    expect(reason == OSMGA_OFFSCREEN_2D_COMMAND_INVALID &&
           validation == OSMGA_COMMAND_RECT_OUT_OF_RANGE,
           "clear-range-reason");

    expect(OSMGAValidateOffscreenCopy32(&transaction, &allocator, &source, 0, 0,
                                        &destination, 0, 0, 64, 32,
                                        &reason, &validation) == 1,
           "separate-offscreen-copy");
    expect(reason == OSMGA_OFFSCREEN_2D_OK, "copy-reason");
    expect(OSMGAValidateOffscreenCopy32(&transaction, &allocator, &source, 0, 0,
                                        &source, 0, 0, 1, 1,
                                        &reason, &validation) == 0,
           "self-copy-rejected");
    expect(reason == OSMGA_OFFSCREEN_2D_SELF_COPY_UNSUPPORTED,
           "self-copy-reason");
    expect(OSMGAReleaseOffscreenSurface(&allocator, source.surface_id,
                                        &allocator_reason) == 1,
           "source-release");
    expect(OSMGAValidateOffscreenClear32(&transaction, &allocator, &source,
                                         0, 0, 1, 1,
                                         &reason, &validation) == 0,
           "released-surface-rejected");
    expect(reason == OSMGA_OFFSCREEN_2D_SURFACE_NOT_LIVE,
           "released-surface-reason");
    expect(OSMGAValidateOffscreenClear32(0, &allocator, &source, 0, 0, 1, 1,
                                         &reason, &validation) == 0,
           "null-transaction-rejected");
    expect(reason == OSMGA_OFFSCREEN_2D_INVALID_ARGUMENT,
           "null-transaction-reason");
    expect(OSMGAOffscreen2DReasonString(OSMGA_OFFSCREEN_2D_OK)[0] == 'o',
           "reason-string");

    if (failures != 0) return 1;
    printf("OPENSTEP_MGA_OFFSCREEN_2D_TEST_STATUS=pass\n");
    return 0;
}
