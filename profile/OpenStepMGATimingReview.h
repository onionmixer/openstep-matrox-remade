/*
 * OpenStepMGATimingReview.h - pure-C offline timing-shape verifier.
 *
 * This declares geometry and blanking data only.  It has no device addresses,
 * register values, or target-system interfaces.
 */

#ifndef OPENSTEP_MGA_TIMING_REVIEW_H
#define OPENSTEP_MGA_TIMING_REVIEW_H

#include "OpenStepMGAEDID.h"

typedef struct {
    OSMGAMode mode;
    unsigned long pixel_clock_khz;
    unsigned long horizontal_front_porch;
    unsigned long horizontal_sync;
    unsigned long horizontal_back_porch;
    unsigned long vertical_front_porch;
    unsigned long vertical_sync;
    unsigned long vertical_back_porch;
    int hsync_positive;
    int vsync_positive;
} OSMGATimingReview;

typedef struct {
    unsigned long horizontal_total;
    unsigned long vertical_total;
    unsigned long calculated_refresh_millihz;
} OSMGATimingReviewResult;

typedef enum {
    OSMGA_TIMING_REVIEW_OK = 0,
    OSMGA_TIMING_REVIEW_INVALID_ARGUMENT,
    OSMGA_TIMING_REVIEW_INVALID_ACTIVE_GEOMETRY,
    OSMGA_TIMING_REVIEW_INVALID_SYNC,
    OSMGA_TIMING_REVIEW_INVALID_POLARITY,
    OSMGA_TIMING_REVIEW_TOTAL_OVERFLOW,
    OSMGA_TIMING_REVIEW_PIXEL_CLOCK_OVERFLOW,
    OSMGA_TIMING_REVIEW_NONINTEGRAL_REFRESH,
    OSMGA_TIMING_REVIEW_REFRESH_MISMATCH
} OSMGATimingReviewReason;

/*
 * Verify that one fully supplied progressive timing shape exactly yields the
 * reviewed manual refresh.  This is an offline arithmetic check only.  It
 * does not select, publish, or program a display mode.
 */
int OSMGAValidateTimingReview(const OSMGATimingReview *review,
                              OSMGATimingReviewResult *result,
                              OSMGATimingReviewReason *reason);

const char *OSMGATimingReviewReasonString(OSMGATimingReviewReason reason);

#endif /* OPENSTEP_MGA_TIMING_REVIEW_H */
