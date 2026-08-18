/* Pure-C offline timing-shape verifier; no target interfaces. */

#include <limits.h>

#include "OpenStepMGATimingReview.h"

static int
add_checked(unsigned long left, unsigned long right, unsigned long *result)
{
    if (left > ULONG_MAX - right) {
        return 0;
    }
    *result = left + right;
    return 1;
}

static int
total_checked(unsigned long active, unsigned long front, unsigned long sync,
              unsigned long back, unsigned long *total)
{
    unsigned long partial;

    if (!add_checked(active, front, &partial) ||
        !add_checked(partial, sync, &partial) ||
        !add_checked(partial, back, total)) {
        return 0;
    }
    return 1;
}

int
OSMGAValidateTimingReview(const OSMGATimingReview *review,
                          OSMGATimingReviewResult *result,
                          OSMGATimingReviewReason *reason)
{
    unsigned long horizontal_total;
    unsigned long vertical_total;
    unsigned long total_pixels;
    unsigned long pixel_clock_hz;
    unsigned long refresh_hz;
    unsigned long refresh_millihz;

    if (reason == 0) {
        return 0;
    }
    if (review == 0) {
        *reason = OSMGA_TIMING_REVIEW_INVALID_ARGUMENT;
        return 0;
    }
    if (review->mode.width == 0 || review->mode.height == 0 ||
        review->mode.refresh_millihz == 0 || review->pixel_clock_khz == 0) {
        *reason = OSMGA_TIMING_REVIEW_INVALID_ACTIVE_GEOMETRY;
        return 0;
    }
    if (review->horizontal_sync == 0 || review->vertical_sync == 0) {
        *reason = OSMGA_TIMING_REVIEW_INVALID_SYNC;
        return 0;
    }
    if ((review->hsync_positive != 0 && review->hsync_positive != 1) ||
        (review->vsync_positive != 0 && review->vsync_positive != 1)) {
        *reason = OSMGA_TIMING_REVIEW_INVALID_POLARITY;
        return 0;
    }
    if (!total_checked((unsigned long)review->mode.width,
                       review->horizontal_front_porch,
                       review->horizontal_sync,
                       review->horizontal_back_porch,
                       &horizontal_total) ||
        !total_checked((unsigned long)review->mode.height,
                       review->vertical_front_porch,
                       review->vertical_sync,
                       review->vertical_back_porch,
                       &vertical_total) ||
        horizontal_total > ULONG_MAX / vertical_total) {
        *reason = OSMGA_TIMING_REVIEW_TOTAL_OVERFLOW;
        return 0;
    }
    total_pixels = horizontal_total * vertical_total;
    if (review->pixel_clock_khz > ULONG_MAX / 1000UL) {
        *reason = OSMGA_TIMING_REVIEW_PIXEL_CLOCK_OVERFLOW;
        return 0;
    }
    pixel_clock_hz = review->pixel_clock_khz * 1000UL;
    if (pixel_clock_hz % total_pixels != 0) {
        *reason = OSMGA_TIMING_REVIEW_NONINTEGRAL_REFRESH;
        return 0;
    }
    refresh_hz = pixel_clock_hz / total_pixels;
    if (refresh_hz > ULONG_MAX / 1000UL) {
        *reason = OSMGA_TIMING_REVIEW_PIXEL_CLOCK_OVERFLOW;
        return 0;
    }
    refresh_millihz = refresh_hz * 1000UL;
    if (refresh_millihz != review->mode.refresh_millihz) {
        *reason = OSMGA_TIMING_REVIEW_REFRESH_MISMATCH;
        return 0;
    }
    if (result != 0) {
        result->horizontal_total = horizontal_total;
        result->vertical_total = vertical_total;
        result->calculated_refresh_millihz = refresh_millihz;
    }
    *reason = OSMGA_TIMING_REVIEW_OK;
    return 1;
}

const char *
OSMGATimingReviewReasonString(OSMGATimingReviewReason reason)
{
    switch (reason) {
    case OSMGA_TIMING_REVIEW_OK:
        return "ok";
    case OSMGA_TIMING_REVIEW_INVALID_ARGUMENT:
        return "invalid-argument";
    case OSMGA_TIMING_REVIEW_INVALID_ACTIVE_GEOMETRY:
        return "invalid-active-geometry";
    case OSMGA_TIMING_REVIEW_INVALID_SYNC:
        return "invalid-sync";
    case OSMGA_TIMING_REVIEW_INVALID_POLARITY:
        return "invalid-polarity";
    case OSMGA_TIMING_REVIEW_TOTAL_OVERFLOW:
        return "total-overflow";
    case OSMGA_TIMING_REVIEW_PIXEL_CLOCK_OVERFLOW:
        return "pixel-clock-overflow";
    case OSMGA_TIMING_REVIEW_NONINTEGRAL_REFRESH:
        return "nonintegral-refresh";
    case OSMGA_TIMING_REVIEW_REFRESH_MISMATCH:
        return "refresh-mismatch";
    default:
        return "unknown";
    }
}
