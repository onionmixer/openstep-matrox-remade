/* Pure-C G450 mode geometry plan; no mapping, I/O, or register interface. */

#include "OpenStepMGAG450CRTCPlan.h"

static int
add_checked(unsigned long first, unsigned long second, unsigned long *result)
{
    unsigned long value;

    value = first + second;
    if (value < first) {
        return 0;
    }
    *result = value;
    return 1;
}

int
OSMGABuildG450CRTCPlan(const OSMGAR3ManualModeReview *review,
                       OSMGAG450CRTCPlan *plan,
                       OSMGAG450CRTCPlanReason *reason)
{
    OSMGAR3ModeReviewReason review_reason;
    unsigned long scanout_bytes;
    unsigned long horizontal_sync_start;
    unsigned long horizontal_sync_end;
    unsigned long horizontal_total;
    unsigned long vertical_sync_start;
    unsigned long vertical_sync_end;
    unsigned long vertical_total;

    if (reason == 0 || plan == 0) {
        return 0;
    }
    if (review == 0) {
        *reason = OSMGA_G450_CRTC_PLAN_INVALID_ARGUMENT;
        return 0;
    }
    if (!OSMGAValidateR3ManualModeReview(review, &scanout_bytes,
                                         &review_reason)) {
        *reason = OSMGA_G450_CRTC_PLAN_R3_REVIEW;
        return 0;
    }
    if (review->bits_per_pixel != 32U) {
        *reason = OSMGA_G450_CRTC_PLAN_UNSUPPORTED_FORMAT;
        return 0;
    }
    if (!add_checked((unsigned long)review->mode.width,
                     review->timing.horizontal_front_porch,
                     &horizontal_sync_start) ||
        !add_checked(horizontal_sync_start, review->timing.horizontal_sync,
                     &horizontal_sync_end) ||
        !add_checked(horizontal_sync_end, review->timing.horizontal_back_porch,
                     &horizontal_total) ||
        !add_checked((unsigned long)review->mode.height,
                     review->timing.vertical_front_porch,
                     &vertical_sync_start) ||
        !add_checked(vertical_sync_start, review->timing.vertical_sync,
                     &vertical_sync_end) ||
        !add_checked(vertical_sync_end, review->timing.vertical_back_porch,
                     &vertical_total)) {
        *reason = OSMGA_G450_CRTC_PLAN_OVERFLOW;
        return 0;
    }

    plan->pixel_clock_khz = review->pixel_clock_khz;
    plan->horizontal_display = review->mode.width;
    plan->horizontal_sync_start = horizontal_sync_start;
    plan->horizontal_sync_end = horizontal_sync_end;
    plan->horizontal_total = horizontal_total;
    plan->vertical_display = review->mode.height;
    plan->vertical_sync_start = vertical_sync_start;
    plan->vertical_sync_end = vertical_sync_end;
    plan->vertical_total = vertical_total;
    plan->pitch_bytes = review->pitch_bytes;
    plan->scanout_bytes = scanout_bytes;
    plan->hsync_positive = review->timing.hsync_positive;
    plan->vsync_positive = review->timing.vsync_positive;
    *reason = OSMGA_G450_CRTC_PLAN_OK;
    return 1;
}

const char *
OSMGAG450CRTCPlanReasonString(OSMGAG450CRTCPlanReason reason)
{
    switch (reason) {
    case OSMGA_G450_CRTC_PLAN_OK:
        return "ok";
    case OSMGA_G450_CRTC_PLAN_INVALID_ARGUMENT:
        return "invalid-argument";
    case OSMGA_G450_CRTC_PLAN_R3_REVIEW:
        return "r3-review";
    case OSMGA_G450_CRTC_PLAN_UNSUPPORTED_FORMAT:
        return "unsupported-format";
    case OSMGA_G450_CRTC_PLAN_OVERFLOW:
        return "overflow";
    }
    return "unknown";
}
