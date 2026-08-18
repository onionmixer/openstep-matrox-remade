/*
 * OpenStepMGAOffscreenAllocator.h - pure-C opaque offscreen surface ledger.
 *
 * This is allocation arithmetic and ID ownership only. It never maps memory,
 * returns an address, or accesses a display device.
 */

#ifndef OPENSTEP_MGA_OFFSCREEN_ALLOCATOR_H
#define OPENSTEP_MGA_OFFSCREEN_ALLOCATOR_H

#include "OpenStepMGAOffscreen2D.h"

#define OSMGA_OFFSCREEN_MAX_SURFACES 16

typedef struct {
    unsigned long arena_bytes;
    unsigned long alignment_bytes;
    int outside_scanout_region_verified;
} OSMGAOffscreenArena;

typedef struct OSMGAOffscreenAllocator {
    unsigned long arena_bytes;
    unsigned long used_bytes;
    unsigned long alignment_bytes;
    unsigned long next_surface_id;
    OSMGAOffscreenSurface active_surfaces[OSMGA_OFFSCREEN_MAX_SURFACES];
} OSMGAOffscreenAllocator;

typedef struct {
    unsigned int width;
    unsigned int height;
    unsigned int stride_pixels;
} OSMGAOffscreenSurfaceRequest;

typedef enum {
    OSMGA_OFFSCREEN_ALLOCATOR_OK = 0,
    OSMGA_OFFSCREEN_ALLOCATOR_INVALID_ARGUMENT,
    OSMGA_OFFSCREEN_ALLOCATOR_ARENA_UNVERIFIED,
    OSMGA_OFFSCREEN_ALLOCATOR_ALIGNMENT_INVALID,
    OSMGA_OFFSCREEN_ALLOCATOR_SURFACE_INVALID,
    OSMGA_OFFSCREEN_ALLOCATOR_CAPACITY_EXHAUSTED,
    OSMGA_OFFSCREEN_ALLOCATOR_SLOT_EXHAUSTED,
    OSMGA_OFFSCREEN_ALLOCATOR_ID_UNKNOWN,
    OSMGA_OFFSCREEN_ALLOCATOR_SURFACE_NOT_LIVE
} OSMGAOffscreenAllocatorReason;

int OSMGAInitializeOffscreenAllocator(const OSMGAOffscreenArena *arena,
                                      OSMGAOffscreenAllocator *allocator,
                                      OSMGAOffscreenAllocatorReason *reason);

int OSMGAAllocateOffscreenSurface(OSMGAOffscreenAllocator *allocator,
                                  const OSMGAOffscreenSurfaceRequest *request,
                                  OSMGAOffscreenSurface *surface,
                                  OSMGAOffscreenAllocatorReason *reason);

int OSMGAReleaseOffscreenSurface(OSMGAOffscreenAllocator *allocator,
                                 unsigned long surface_id,
                                 OSMGAOffscreenAllocatorReason *reason);

/* Confirm that a complete surface record is still owned by this ledger. */
int OSMGAValidateLiveOffscreenSurface(
    const OSMGAOffscreenAllocator *allocator,
    const OSMGAOffscreenSurface *surface,
    OSMGAOffscreenAllocatorReason *reason);

const char *OSMGAOffscreenAllocatorReasonString(
    OSMGAOffscreenAllocatorReason reason);

#endif /* OPENSTEP_MGA_OFFSCREEN_ALLOCATOR_H */
