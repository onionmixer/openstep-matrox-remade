/* Pure-C opaque offscreen ledger; no target interfaces. */

#include "OpenStepMGAOffscreenAllocator.h"

#define OSMGA_OFFSCREEN_U32_MAX 4294967295UL

static int
valid_alignment(unsigned long value)
{
    return value != 0 && (value & (value - 1UL)) == 0;
}

static int
find_empty_slot(const OSMGAOffscreenAllocator *allocator, unsigned int *slot)
{
    unsigned int index;

    for (index = 0; index < OSMGA_OFFSCREEN_MAX_SURFACES; index++) {
        if (allocator->active_surfaces[index].surface_id == 0) {
            *slot = index;
            return 1;
        }
    }
    return 0;
}

static int
same_surface(const OSMGAOffscreenSurface *left,
             const OSMGAOffscreenSurface *right)
{
    return left->surface_id == right->surface_id &&
           left->geometry.width == right->geometry.width &&
           left->geometry.height == right->geometry.height &&
           left->geometry.stride_pixels == right->geometry.stride_pixels &&
           left->geometry.allocation_bytes == right->geometry.allocation_bytes &&
           left->kernel_allocation_verified == right->kernel_allocation_verified &&
           left->outside_scanout_verified == right->outside_scanout_verified;
}

int
OSMGAInitializeOffscreenAllocator(const OSMGAOffscreenArena *arena,
                                  OSMGAOffscreenAllocator *allocator,
                                  OSMGAOffscreenAllocatorReason *reason)
{
    unsigned int index;

    if (reason == 0 || allocator == 0 || arena == 0) {
        return 0;
    }
    if (!arena->outside_scanout_region_verified) {
        *reason = OSMGA_OFFSCREEN_ALLOCATOR_ARENA_UNVERIFIED;
        return 0;
    }
    if (arena->arena_bytes == 0 || !valid_alignment(arena->alignment_bytes)) {
        *reason = OSMGA_OFFSCREEN_ALLOCATOR_ALIGNMENT_INVALID;
        return 0;
    }
    allocator->arena_bytes = arena->arena_bytes;
    allocator->used_bytes = 0;
    allocator->alignment_bytes = arena->alignment_bytes;
    allocator->next_surface_id = 1;
    for (index = 0; index < OSMGA_OFFSCREEN_MAX_SURFACES; index++) {
        allocator->active_surfaces[index].surface_id = 0;
    }
    *reason = OSMGA_OFFSCREEN_ALLOCATOR_OK;
    return 1;
}

int
OSMGAAllocateOffscreenSurface(OSMGAOffscreenAllocator *allocator,
                              const OSMGAOffscreenSurfaceRequest *request,
                              OSMGAOffscreenSurface *surface,
                              OSMGAOffscreenAllocatorReason *reason)
{
    OSMGASurfaceGeometry geometry;
    OSMGACommandValidation validation;
    unsigned long required_bytes;
    unsigned long remainder;
    unsigned long aligned_bytes;
    unsigned int slot;

    if (reason == 0 || allocator == 0 || request == 0 || surface == 0) {
        return 0;
    }
    if (allocator->arena_bytes == 0 || !valid_alignment(allocator->alignment_bytes)) {
        *reason = OSMGA_OFFSCREEN_ALLOCATOR_INVALID_ARGUMENT;
        return 0;
    }
    geometry.width = request->width;
    geometry.height = request->height;
    geometry.stride_pixels = request->stride_pixels;
    geometry.allocation_bytes = OSMGA_OFFSCREEN_U32_MAX;
    required_bytes = 0;
    if (!OSMGAValidateSurface32(&geometry, &required_bytes, &validation)) {
        *reason = OSMGA_OFFSCREEN_ALLOCATOR_SURFACE_INVALID;
        return 0;
    }
    remainder = required_bytes % allocator->alignment_bytes;
    aligned_bytes = required_bytes;
    if (remainder != 0) {
        if (aligned_bytes > OSMGA_OFFSCREEN_U32_MAX -
                            (allocator->alignment_bytes - remainder)) {
            *reason = OSMGA_OFFSCREEN_ALLOCATOR_SURFACE_INVALID;
            return 0;
        }
        aligned_bytes += allocator->alignment_bytes - remainder;
    }
    if (allocator->used_bytes > allocator->arena_bytes ||
        aligned_bytes > allocator->arena_bytes - allocator->used_bytes) {
        *reason = OSMGA_OFFSCREEN_ALLOCATOR_CAPACITY_EXHAUSTED;
        return 0;
    }
    if (!find_empty_slot(allocator, &slot)) {
        *reason = OSMGA_OFFSCREEN_ALLOCATOR_SLOT_EXHAUSTED;
        return 0;
    }
    if (allocator->next_surface_id == 0) {
        *reason = OSMGA_OFFSCREEN_ALLOCATOR_SLOT_EXHAUSTED;
        return 0;
    }
    surface->surface_id = allocator->next_surface_id;
    surface->geometry = geometry;
    surface->geometry.allocation_bytes = required_bytes;
    surface->kernel_allocation_verified = 1;
    surface->outside_scanout_verified = 1;
    allocator->active_surfaces[slot] = *surface;
    allocator->next_surface_id++;
    allocator->used_bytes += aligned_bytes;
    *reason = OSMGA_OFFSCREEN_ALLOCATOR_OK;
    return 1;
}

int
OSMGAReleaseOffscreenSurface(OSMGAOffscreenAllocator *allocator,
                             unsigned long surface_id,
                             OSMGAOffscreenAllocatorReason *reason)
{
    unsigned int index;

    if (reason == 0 || allocator == 0 || surface_id == 0) {
        return 0;
    }
    for (index = 0; index < OSMGA_OFFSCREEN_MAX_SURFACES; index++) {
        if (allocator->active_surfaces[index].surface_id == surface_id) {
            allocator->active_surfaces[index].surface_id = 0;
            *reason = OSMGA_OFFSCREEN_ALLOCATOR_OK;
            return 1;
        }
    }
    *reason = OSMGA_OFFSCREEN_ALLOCATOR_ID_UNKNOWN;
    return 0;
}

int
OSMGAValidateLiveOffscreenSurface(
    const OSMGAOffscreenAllocator *allocator,
    const OSMGAOffscreenSurface *surface,
    OSMGAOffscreenAllocatorReason *reason)
{
    unsigned int index;

    if (reason == 0) {
        return 0;
    }
    if (allocator == 0 || surface == 0) {
        *reason = OSMGA_OFFSCREEN_ALLOCATOR_INVALID_ARGUMENT;
        return 0;
    }
    if (surface->surface_id == 0) {
        *reason = OSMGA_OFFSCREEN_ALLOCATOR_SURFACE_NOT_LIVE;
        return 0;
    }
    for (index = 0; index < OSMGA_OFFSCREEN_MAX_SURFACES; index++) {
        if (allocator->active_surfaces[index].surface_id == surface->surface_id) {
            if (same_surface(&allocator->active_surfaces[index], surface)) {
                *reason = OSMGA_OFFSCREEN_ALLOCATOR_OK;
                return 1;
            }
            *reason = OSMGA_OFFSCREEN_ALLOCATOR_SURFACE_NOT_LIVE;
            return 0;
        }
    }
    *reason = OSMGA_OFFSCREEN_ALLOCATOR_SURFACE_NOT_LIVE;
    return 0;
}

const char *
OSMGAOffscreenAllocatorReasonString(OSMGAOffscreenAllocatorReason reason)
{
    switch (reason) {
    case OSMGA_OFFSCREEN_ALLOCATOR_OK: return "ok";
    case OSMGA_OFFSCREEN_ALLOCATOR_INVALID_ARGUMENT: return "invalid-argument";
    case OSMGA_OFFSCREEN_ALLOCATOR_ARENA_UNVERIFIED: return "arena-unverified";
    case OSMGA_OFFSCREEN_ALLOCATOR_ALIGNMENT_INVALID: return "alignment-invalid";
    case OSMGA_OFFSCREEN_ALLOCATOR_SURFACE_INVALID: return "surface-invalid";
    case OSMGA_OFFSCREEN_ALLOCATOR_CAPACITY_EXHAUSTED: return "capacity-exhausted";
    case OSMGA_OFFSCREEN_ALLOCATOR_SLOT_EXHAUSTED: return "slot-exhausted";
    case OSMGA_OFFSCREEN_ALLOCATOR_ID_UNKNOWN: return "id-unknown";
    case OSMGA_OFFSCREEN_ALLOCATOR_SURFACE_NOT_LIVE: return "surface-not-live";
    }
    return "unknown";
}
