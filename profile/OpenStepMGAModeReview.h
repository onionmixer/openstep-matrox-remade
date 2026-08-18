/*
 * OpenStepMGAModeReview.h - offline admission policy for one R3 manual mode.
 *
 * This type deliberately contains no register values or device addresses.
 * It makes the evidence and arithmetic required before a future, separately
 * approved mode-programming implementation can be reviewed.
 */

#ifndef OPENSTEP_MGA_MODE_REVIEW_H
#define OPENSTEP_MGA_MODE_REVIEW_H

#include "OpenStepMGAProfile.h"
#include "OpenStepMGAEDID.h"
#include "OpenStepMGATimingReview.h"

enum {
    OSMGA_R3_EVIDENCE_MODE_SOURCE = 1UL << 0,
    OSMGA_R3_EVIDENCE_TIMING_SOURCE = 1UL << 1,
    OSMGA_R3_EVIDENCE_PITCH_POLICY = 1UL << 2,
    OSMGA_R3_EVIDENCE_MAPPING_BOUND = 1UL << 3
};

typedef struct {
    OSMGAR2PhysicalProfile physical_profile;
    unsigned long configured_vram_bytes;
    unsigned long evidence_mask;
    OSMGAMode mode;
    OSMGATimingReview timing;
    unsigned int bits_per_pixel;
    unsigned long pitch_bytes;
    unsigned long pitch_alignment_bytes;
    unsigned long pixel_clock_khz;
    unsigned long mapping_bytes;
} OSMGAR3ManualModeReview;

typedef enum {
    OSMGA_R3_MODE_REVIEW_OK = 0,
    OSMGA_R3_MODE_REVIEW_INVALID_ARGUMENT,
    OSMGA_R3_MODE_REVIEW_R2_PROFILE,
    OSMGA_R3_MODE_REVIEW_MANUAL_MEMORY_MISMATCH,
    OSMGA_R3_MODE_REVIEW_MODE_SOURCE_UNVERIFIED,
    OSMGA_R3_MODE_REVIEW_TIMING_SOURCE_UNVERIFIED,
    OSMGA_R3_MODE_REVIEW_TIMING_RECORD_INVALID,
    OSMGA_R3_MODE_REVIEW_TIMING_RECORD_MISMATCH,
    OSMGA_R3_MODE_REVIEW_PITCH_POLICY_UNVERIFIED,
    OSMGA_R3_MODE_REVIEW_MAPPING_BOUND_UNVERIFIED,
    OSMGA_R3_MODE_REVIEW_INVALID_MODE,
    OSMGA_R3_MODE_REVIEW_INVALID_PIXEL_CLOCK,
    OSMGA_R3_MODE_REVIEW_PIXEL_CLOCK_EXCEEDS_LIMIT,
    OSMGA_R3_MODE_REVIEW_INVALID_ALIGNMENT,
    OSMGA_R3_MODE_REVIEW_MISALIGNED_PITCH,
    OSMGA_R3_MODE_REVIEW_FRAMEBUFFER_FOOTPRINT,
    OSMGA_R3_MODE_REVIEW_MAPPING_TOO_SMALL,
    OSMGA_R3_MODE_REVIEW_MAPPING_EXCEEDS_VRAM
} OSMGAR3ModeReviewReason;

/*
 * Validate exactly one proposed manual mode entirely offline.  `required_bytes`
 * is assigned only on success.  Passing does not authorize mapping, mode
 * programming, or replacement-driver installation; it only completes the
 * arithmetic/evidence portion of the R3 review.
 */
int OSMGAValidateR3ManualModeReview(const OSMGAR3ManualModeReview *review,
                                    unsigned long *required_bytes,
                                    OSMGAR3ModeReviewReason *reason);

const char *OSMGAR3ModeReviewReasonString(OSMGAR3ModeReviewReason reason);

#endif /* OPENSTEP_MGA_MODE_REVIEW_H */
