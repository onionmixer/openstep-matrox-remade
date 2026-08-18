#include <stdio.h>

#include "OpenStepMGAOffscreenAllocator.h"

static int failures;

static void
expect(int condition, const char *name)
{
    if (!condition) {
        printf("OPENSTEP_MGA_OFFSCREEN_ALLOCATOR_TEST=fail:%s\n", name);
        failures++;
    }
}

int
main(void)
{
    OSMGAOffscreenArena arena;
    OSMGAOffscreenAllocator allocator;
    OSMGAOffscreenSurfaceRequest request;
    OSMGAOffscreenSurface first;
    OSMGAOffscreenSurface second;
    OSMGAOffscreenSurface slots[OSMGA_OFFSCREEN_MAX_SURFACES];
    OSMGAOffscreenAllocatorReason reason;
    unsigned int index;

    arena.arena_bytes = 16384UL;
    arena.alignment_bytes = 4096UL;
    arena.outside_scanout_region_verified = 0;
    expect(OSMGAInitializeOffscreenAllocator(&arena, &allocator, &reason) == 0,
           "unverified-arena-rejected");
    expect(reason == OSMGA_OFFSCREEN_ALLOCATOR_ARENA_UNVERIFIED,
           "unverified-arena-reason");

    arena.outside_scanout_region_verified = 1;
    arena.alignment_bytes = 3UL;
    expect(OSMGAInitializeOffscreenAllocator(&arena, &allocator, &reason) == 0,
           "non-power-of-two-alignment-rejected");
    expect(reason == OSMGA_OFFSCREEN_ALLOCATOR_ALIGNMENT_INVALID,
           "non-power-of-two-alignment-reason");

    arena.outside_scanout_region_verified = 1;
    arena.alignment_bytes = 4096UL;
    expect(OSMGAInitializeOffscreenAllocator(&arena, &allocator, &reason) == 1,
           "allocator-init");
    expect(OSMGAValidateLiveOffscreenSurface(0, &first, &reason) == 0,
           "null-allocator-rejected");
    expect(reason == OSMGA_OFFSCREEN_ALLOCATOR_INVALID_ARGUMENT,
           "null-allocator-reason");
    request.width = 64;
    request.height = 32;
    request.stride_pixels = 64;
    expect(OSMGAAllocateOffscreenSurface(&allocator, &request, &first, &reason) == 1,
           "first-allocation");
    expect(first.surface_id == 1UL && first.geometry.allocation_bytes == 8192UL &&
           allocator.used_bytes == 8192UL, "first-allocation-record");
    expect(first.kernel_allocation_verified && first.outside_scanout_verified,
           "first-allocation-evidence");
    expect(OSMGAValidateLiveOffscreenSurface(&allocator, &first, &reason) == 1,
           "first-live-surface");
    expect(OSMGAAllocateOffscreenSurface(&allocator, &request, &second, &reason) == 1,
           "second-allocation");
    expect(second.surface_id == 2UL && allocator.used_bytes == 16384UL,
           "second-allocation-record");
    expect(OSMGAAllocateOffscreenSurface(&allocator, &request, &second, &reason) == 0,
           "capacity-enforced");
    expect(reason == OSMGA_OFFSCREEN_ALLOCATOR_CAPACITY_EXHAUSTED,
           "capacity-reason");
    expect(OSMGAReleaseOffscreenSurface(&allocator, first.surface_id, &reason) == 1,
           "release-known-id");
    expect(OSMGAValidateLiveOffscreenSurface(&allocator, &first, &reason) == 0,
           "released-surface-not-live");
    expect(reason == OSMGA_OFFSCREEN_ALLOCATOR_SURFACE_NOT_LIVE,
           "released-surface-reason");
    expect(OSMGAReleaseOffscreenSurface(&allocator, first.surface_id, &reason) == 0,
           "release-twice-rejected");
    expect(reason == OSMGA_OFFSCREEN_ALLOCATOR_ID_UNKNOWN,
           "release-twice-reason");
    request.stride_pixels = 63;
    expect(OSMGAAllocateOffscreenSurface(&allocator, &request, &first, &reason) == 0,
           "invalid-geometry-rejected");
    expect(reason == OSMGA_OFFSCREEN_ALLOCATOR_SURFACE_INVALID,
           "invalid-geometry-reason");
    second.geometry.width = 63;
    expect(OSMGAValidateLiveOffscreenSurface(&allocator, &second, &reason) == 0,
           "mutated-surface-not-live");
    expect(reason == OSMGA_OFFSCREEN_ALLOCATOR_SURFACE_NOT_LIVE,
           "mutated-surface-reason");
    expect(OSMGAOffscreenAllocatorReasonString(OSMGA_OFFSCREEN_ALLOCATOR_OK)[0] == 'o',
           "reason-string");

    arena.arena_bytes = 128UL;
    arena.alignment_bytes = 1UL;
    expect(OSMGAInitializeOffscreenAllocator(&arena, &allocator, &reason) == 1,
           "slot-fixture-init");
    request.width = 1;
    request.height = 1;
    request.stride_pixels = 1;
    for (index = 0; index < OSMGA_OFFSCREEN_MAX_SURFACES; index++) {
        expect(OSMGAAllocateOffscreenSurface(&allocator, &request,
                                             &slots[index], &reason) == 1,
               "slot-allocation");
    }
    expect(OSMGAAllocateOffscreenSurface(&allocator, &request, &first, &reason) == 0,
           "slot-capacity-enforced");
    expect(reason == OSMGA_OFFSCREEN_ALLOCATOR_SLOT_EXHAUSTED,
           "slot-capacity-reason");

    if (failures != 0) return 1;
    printf("OPENSTEP_MGA_OFFSCREEN_ALLOCATOR_TEST_STATUS=pass\n");
    return 0;
}
