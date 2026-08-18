#include <stdio.h>

#include "OpenStepMGATimingReview.h"

static int failures;

static void
expect(int condition, const char *name)
{
    if (!condition) {
        printf("OPENSTEP_MGA_TIMING_REVIEW_TEST=fail:%s\n", name);
        failures++;
    }
}

static void
make_dmt_1600x1200_60(OSMGATimingReview *review)
{
    review->mode.width = 1600;
    review->mode.height = 1200;
    review->mode.refresh_millihz = 60000UL;
    review->pixel_clock_khz = 162000UL;
    review->horizontal_front_porch = 64UL;
    review->horizontal_sync = 192UL;
    review->horizontal_back_porch = 304UL;
    review->vertical_front_porch = 1UL;
    review->vertical_sync = 3UL;
    review->vertical_back_porch = 46UL;
    review->hsync_positive = 1;
    review->vsync_positive = 1;
}

int
main(void)
{
    OSMGATimingReview review;
    OSMGATimingReviewResult result;
    OSMGATimingReviewReason reason;

    expect(OSMGAValidateTimingReview(0, &result, &reason) == 0,
           "null-review-rejected");
    expect(reason == OSMGA_TIMING_REVIEW_INVALID_ARGUMENT,
           "null-review-reason");

    make_dmt_1600x1200_60(&review);
    expect(OSMGAValidateTimingReview(&review, &result, &reason) == 1,
           "dmt-1600x1200-60-accepted");
    expect(reason == OSMGA_TIMING_REVIEW_OK, "dmt-review-reason");
    expect(result.horizontal_total == 2160UL, "dmt-horizontal-total");
    expect(result.vertical_total == 1250UL, "dmt-vertical-total");
    expect(result.calculated_refresh_millihz == 60000UL,
           "dmt-calculated-refresh");

    make_dmt_1600x1200_60(&review);
    review.mode.refresh_millihz = 59000UL;
    expect(OSMGAValidateTimingReview(&review, &result, &reason) == 0,
           "refresh-mismatch-rejected");
    expect(reason == OSMGA_TIMING_REVIEW_REFRESH_MISMATCH,
           "refresh-mismatch-reason");

    make_dmt_1600x1200_60(&review);
    review.horizontal_sync = 0;
    expect(OSMGAValidateTimingReview(&review, &result, &reason) == 0,
           "zero-sync-rejected");
    expect(reason == OSMGA_TIMING_REVIEW_INVALID_SYNC,
           "zero-sync-reason");

    make_dmt_1600x1200_60(&review);
    review.hsync_positive = 2;
    expect(OSMGAValidateTimingReview(&review, &result, &reason) == 0,
           "invalid-polarity-rejected");
    expect(reason == OSMGA_TIMING_REVIEW_INVALID_POLARITY,
           "invalid-polarity-reason");

    make_dmt_1600x1200_60(&review);
    review.pixel_clock_khz = 162001UL;
    expect(OSMGAValidateTimingReview(&review, &result, &reason) == 0,
           "nonintegral-refresh-rejected");
    expect(reason == OSMGA_TIMING_REVIEW_NONINTEGRAL_REFRESH,
           "nonintegral-refresh-reason");
    expect(OSMGATimingReviewReasonString(reason)[0] == 'n', "reason-string");

    if (failures != 0) {
        return 1;
    }
    printf("OPENSTEP_MGA_TIMING_REVIEW_TEST_STATUS=pass\n");
    return 0;
}
