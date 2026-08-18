/*
 * OpenStepMGAG450RangePlan.h - reviewed legacy resource publication plan.
 *
 * This plan is data only. It does not publish a DriverKit range list, map a
 * physical range, or access framebuffer/VGA memory.
 */

#ifndef OPENSTEP_MGA_G450_RANGE_PLAN_H
#define OPENSTEP_MGA_G450_RANGE_PLAN_H

#include "OpenStepMGAMappingReview.h"

#define OSMGA_G450_LEGACY_RANGE_COUNT 3U

typedef struct {
    unsigned long physical_start;
    unsigned long length_bytes;
} OSMGAPhysicalRangePlan;

typedef struct {
    OSMGAPhysicalRangePlan ranges[OSMGA_G450_LEGACY_RANGE_COUNT];
    unsigned int framebuffer_index;
    unsigned int vga_index;
    unsigned int bios_index;
} OSMGAG450RangePlan;

typedef enum {
    OSMGA_G450_RANGE_PLAN_OK = 0,
    OSMGA_G450_RANGE_PLAN_INVALID_ARGUMENT,
    OSMGA_G450_RANGE_PLAN_MAPPING_REVIEW,
    OSMGA_G450_RANGE_PLAN_FRAMEBUFFER_START,
    OSMGA_G450_RANGE_PLAN_FRAMEBUFFER_LENGTH,
    OSMGA_G450_RANGE_PLAN_RANGE_LAYOUT
} OSMGAG450RangePlanReason;

/*
 * Make the one framebuffer + legacy VGA + legacy BIOS publication plan from
 * a reviewed 16 MiB record and a separately verified page-aligned physical
 * framebuffer base. The caller owns provenance of the base address.
 */
int OSMGABuildG450LegacyRangePlan(const OSMGAR6MappingReview *review,
                                  unsigned long framebuffer_physical_start,
                                  OSMGAG450RangePlan *plan,
                                  OSMGAG450RangePlanReason *reason);

const char *OSMGAG450RangePlanReasonString(OSMGAG450RangePlanReason reason);

#endif /* OPENSTEP_MGA_G450_RANGE_PLAN_H */
