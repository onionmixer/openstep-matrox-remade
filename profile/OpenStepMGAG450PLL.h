/*
 * OpenStepMGAG450PLL.h - offline G450 pixel-clock candidate arithmetic.
 *
 * This module deliberately returns frequency-plan fields, not DAC register
 * bytes.  It has no target interface and cannot program a PLL.
 */

#ifndef OPENSTEP_MGA_G450_PLL_H
#define OPENSTEP_MGA_G450_PLL_H

#include "OpenStepMGAModeReview.h"

typedef enum {
    OSMGA_G450_HEAD_UNKNOWN = 0,
    OSMGA_G450_HEAD_PRIMARY,
    OSMGA_G450_HEAD_SECONDARY
} OSMGAG450Head;

typedef struct {
    OSMGAR3ManualModeReview mode_review;
    int pll_source_verified;
    int head_selection_verified;
    OSMGAG450Head head;
} OSMGAG450PLLReview;

typedef struct {
    unsigned long requested_khz;
    unsigned long achieved_khz;
    unsigned long error_ppm;
    unsigned long vco_khz;
    unsigned int feedback_divider;
    unsigned int reference_divider;
    unsigned int post_divider;
} OSMGAG450PLLPlan;

typedef enum {
    OSMGA_G450_PLL_OK = 0,
    OSMGA_G450_PLL_INVALID_ARGUMENT,
    OSMGA_G450_PLL_R3_MODE,
    OSMGA_G450_PLL_SOURCE_UNVERIFIED,
    OSMGA_G450_PLL_HEAD_UNVERIFIED,
    OSMGA_G450_PLL_HEAD_INVALID,
    OSMGA_G450_PLL_NO_CANDIDATE
} OSMGAG450PLLReason;

/*
 * Produce the lowest-error offline frequency candidate for the R3 pixel
 * clock.  Success is not authority to write any DAC/MMIO/port register.
 */
int OSMGAPlanG450PixelPLL(const OSMGAG450PLLReview *review,
                          OSMGAG450PLLPlan *plan,
                          OSMGAG450PLLReason *reason);

const char *OSMGAG450PLLReasonString(OSMGAG450PLLReason reason);

#endif /* OPENSTEP_MGA_G450_PLL_H */
