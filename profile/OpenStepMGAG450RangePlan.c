/* Pure-C legacy range publication plan; no target interfaces. */

#include "OpenStepMGAG450RangePlan.h"

#define OSMGA_G450_DEPLOYMENT_BYTES (16UL * 1024UL * 1024UL)
#define OSMGA_G450_PAGE_MASK 0x0fffUL
#define OSMGA_G450_VGA_START 0x000a0000UL
#define OSMGA_G450_VGA_BYTES 0x00020000UL
#define OSMGA_G450_BIOS_START 0x000c0000UL
#define OSMGA_G450_BIOS_BYTES 0x00010000UL

int
OSMGABuildG450LegacyRangePlan(const OSMGAR6MappingReview *review,
                              unsigned long framebuffer_physical_start,
                              OSMGAG450RangePlan *plan,
                              OSMGAG450RangePlanReason *reason)
{
    OSMGAR6MappingReviewReason mapping_reason;

    if (reason == 0 || plan == 0) {
        return 0;
    }
    if (review == 0) {
        *reason = OSMGA_G450_RANGE_PLAN_INVALID_ARGUMENT;
        return 0;
    }
    if (!OSMGAValidateR6MappingReview(review, &mapping_reason)) {
        *reason = OSMGA_G450_RANGE_PLAN_MAPPING_REVIEW;
        return 0;
    }
    if (framebuffer_physical_start == 0UL ||
        (framebuffer_physical_start & OSMGA_G450_PAGE_MASK) != 0UL) {
        *reason = OSMGA_G450_RANGE_PLAN_FRAMEBUFFER_START;
        return 0;
    }
    if (review->framebuffer_range_bytes != OSMGA_G450_DEPLOYMENT_BYTES ||
        review->mode_review.mapping_bytes != OSMGA_G450_DEPLOYMENT_BYTES) {
        *reason = OSMGA_G450_RANGE_PLAN_FRAMEBUFFER_LENGTH;
        return 0;
    }
    if (review->memory_range_count != OSMGA_G450_LEGACY_RANGE_COUNT ||
        review->framebuffer_range_index != 0U) {
        *reason = OSMGA_G450_RANGE_PLAN_RANGE_LAYOUT;
        return 0;
    }

    plan->framebuffer_index = 0U;
    plan->vga_index = 1U;
    plan->bios_index = 2U;
    plan->ranges[plan->framebuffer_index].physical_start =
        framebuffer_physical_start;
    plan->ranges[plan->framebuffer_index].length_bytes =
        OSMGA_G450_DEPLOYMENT_BYTES;
    plan->ranges[plan->vga_index].physical_start = OSMGA_G450_VGA_START;
    plan->ranges[plan->vga_index].length_bytes = OSMGA_G450_VGA_BYTES;
    plan->ranges[plan->bios_index].physical_start = OSMGA_G450_BIOS_START;
    plan->ranges[plan->bios_index].length_bytes = OSMGA_G450_BIOS_BYTES;
    *reason = OSMGA_G450_RANGE_PLAN_OK;
    return 1;
}

const char *
OSMGAG450RangePlanReasonString(OSMGAG450RangePlanReason reason)
{
    switch (reason) {
    case OSMGA_G450_RANGE_PLAN_OK:
        return "ok";
    case OSMGA_G450_RANGE_PLAN_INVALID_ARGUMENT:
        return "invalid-argument";
    case OSMGA_G450_RANGE_PLAN_MAPPING_REVIEW:
        return "mapping-review";
    case OSMGA_G450_RANGE_PLAN_FRAMEBUFFER_START:
        return "framebuffer-start";
    case OSMGA_G450_RANGE_PLAN_FRAMEBUFFER_LENGTH:
        return "framebuffer-length";
    case OSMGA_G450_RANGE_PLAN_RANGE_LAYOUT:
        return "range-layout";
    }
    return "unknown";
}
