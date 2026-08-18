#include <stdio.h>

#include "OpenStepMGAModeReview.h"

static int failures;

static void
expect(int condition, const char *name)
{
    if (!condition) {
        printf("OPENSTEP_MGA_R3_MODE_REVIEW_TEST=fail:%s\n", name);
        failures++;
    }
}

static void
make_complete_review(OSMGAR3ManualModeReview *review)
{
    review->physical_profile.evidence_mask =
        OSMGA_R2_EVIDENCE_BOARD_ID |
        OSMGA_R2_EVIDENCE_INDEPENDENT_CROSSCHECK |
        OSMGA_R2_EVIDENCE_VRAM_TYPE |
        OSMGA_R2_EVIDENCE_VRAM_SIZE |
        OSMGA_R2_EVIDENCE_RAMDAC_LIMIT;
    review->physical_profile.board_evidence_reference = "B2";
    review->physical_profile.crosscheck_evidence_reference = "B6";
    review->physical_profile.vram_evidence_reference = "B4+B5";
    review->physical_profile.ramdac_evidence_reference = "B5";
    review->physical_profile.vram_type = OSMGA_VRAM_TYPE_DDR_SDRAM;
    review->physical_profile.physical_vram_bytes = 16UL * 1024UL * 1024UL;
    review->physical_profile.applicable_ramdac_khz = 300000UL;
    review->configured_vram_bytes = 16UL * 1024UL * 1024UL;
    review->evidence_mask = OSMGA_R3_EVIDENCE_MODE_SOURCE |
                            OSMGA_R3_EVIDENCE_TIMING_SOURCE |
                            OSMGA_R3_EVIDENCE_PITCH_POLICY |
                            OSMGA_R3_EVIDENCE_MAPPING_BOUND;
    review->mode.width = 1600;
    review->mode.height = 1200;
    review->mode.refresh_millihz = 60000UL;
    review->timing.mode = review->mode;
    review->timing.pixel_clock_khz = 162000UL;
    review->timing.horizontal_front_porch = 64UL;
    review->timing.horizontal_sync = 192UL;
    review->timing.horizontal_back_porch = 304UL;
    review->timing.vertical_front_porch = 1UL;
    review->timing.vertical_sync = 3UL;
    review->timing.vertical_back_porch = 46UL;
    review->timing.hsync_positive = 1;
    review->timing.vsync_positive = 1;
    review->bits_per_pixel = 32;
    review->pitch_bytes = 6400UL;
    review->pitch_alignment_bytes = 8UL;
    review->pixel_clock_khz = 162000UL;
    review->mapping_bytes = 16UL * 1024UL * 1024UL;
}

/*
 * This is deliberately a policy-only fixture.  The 8 MiB capacity is an
 * operator-provided working assumption, and the 162 MHz timing value is not a
 * target timing claim.  Real R2/R3 evidence must replace every symbolic
 * reference before a target mode table can be reviewed.
 */
static void
make_synthetic_8m_current_mode_review(OSMGAR3ManualModeReview *review)
{
    make_complete_review(review);
    review->physical_profile.physical_vram_bytes = 8UL * 1024UL * 1024UL;
    review->configured_vram_bytes = 8UL * 1024UL * 1024UL;
    review->mode.width = 1600;
    review->mode.height = 1200;
    review->mode.refresh_millihz = 60000UL;
    review->bits_per_pixel = 32;
    review->pitch_bytes = 6400UL;
    review->pitch_alignment_bytes = 8UL;
    review->pixel_clock_khz = 162000UL;
    review->mapping_bytes = 8UL * 1024UL * 1024UL;
}

int
main(void)
{
    OSMGAR3ManualModeReview review;
    OSMGAR3ModeReviewReason reason;
    unsigned long required;

    required = 0;
    expect(OSMGAValidateR3ManualModeReview(0, &required, &reason) == 0,
           "null-review-rejected");
    expect(reason == OSMGA_R3_MODE_REVIEW_INVALID_ARGUMENT,
           "null-review-reason");

    make_complete_review(&review);
    review.physical_profile.evidence_mask = 0;
    expect(OSMGAValidateR3ManualModeReview(&review, &required, &reason) == 0,
           "r2-profile-required");
    expect(reason == OSMGA_R3_MODE_REVIEW_R2_PROFILE, "r2-profile-reason");

    make_complete_review(&review);
    review.configured_vram_bytes = 8UL * 1024UL * 1024UL;
    expect(OSMGAValidateR3ManualModeReview(&review, &required, &reason) == 0,
           "manual-memory-mismatch-rejected");
    expect(reason == OSMGA_R3_MODE_REVIEW_MANUAL_MEMORY_MISMATCH,
           "manual-memory-mismatch-reason");

    make_complete_review(&review);
    review.evidence_mask &= ~OSMGA_R3_EVIDENCE_TIMING_SOURCE;
    expect(OSMGAValidateR3ManualModeReview(&review, &required, &reason) == 0,
           "timing-evidence-required");
    expect(reason == OSMGA_R3_MODE_REVIEW_TIMING_SOURCE_UNVERIFIED,
           "timing-evidence-reason");

    make_complete_review(&review);
    review.mode.refresh_millihz = 120000UL;
    review.timing.mode = review.mode;
    review.pixel_clock_khz = 324000UL;
    review.timing.pixel_clock_khz = 324000UL;
    expect(OSMGAValidateR3ManualModeReview(&review, &required, &reason) == 0,
           "ramdac-limit-enforced");
    expect(reason == OSMGA_R3_MODE_REVIEW_PIXEL_CLOCK_EXCEEDS_LIMIT,
           "ramdac-limit-reason");

    make_complete_review(&review);
    review.timing.horizontal_back_porch = 303UL;
    expect(OSMGAValidateR3ManualModeReview(&review, &required, &reason) == 0,
           "timing-record-invalid-rejected");
    expect(reason == OSMGA_R3_MODE_REVIEW_TIMING_RECORD_INVALID,
           "timing-record-invalid-reason");

    make_complete_review(&review);
    review.timing.mode.width = 1440;
    review.timing.horizontal_back_porch = 464UL;
    expect(OSMGAValidateR3ManualModeReview(&review, &required, &reason) == 0,
           "timing-record-mismatch-rejected");
    expect(reason == OSMGA_R3_MODE_REVIEW_TIMING_RECORD_MISMATCH,
           "timing-record-mismatch-reason");

    make_complete_review(&review);
    review.pitch_bytes = 6401UL;
    expect(OSMGAValidateR3ManualModeReview(&review, &required, &reason) == 0,
           "pitch-alignment-enforced");
    expect(reason == OSMGA_R3_MODE_REVIEW_MISALIGNED_PITCH,
           "pitch-alignment-reason");

    make_complete_review(&review);
    review.mapping_bytes = 7679999UL;
    expect(OSMGAValidateR3ManualModeReview(&review, &required, &reason) == 0,
           "mapping-minimum-enforced");
    expect(reason == OSMGA_R3_MODE_REVIEW_MAPPING_TOO_SMALL,
           "mapping-minimum-reason");

    make_complete_review(&review);
    review.mapping_bytes = (16UL * 1024UL * 1024UL) + 1UL;
    expect(OSMGAValidateR3ManualModeReview(&review, &required, &reason) == 0,
           "mapping-vram-bound-enforced");
    expect(reason == OSMGA_R3_MODE_REVIEW_MAPPING_EXCEEDS_VRAM,
           "mapping-vram-bound-reason");

    make_complete_review(&review);
    expect(OSMGAValidateR3ManualModeReview(&review, &required, &reason) == 1,
           "complete-synthetic-review-accepted");
    expect(reason == OSMGA_R3_MODE_REVIEW_OK, "complete-review-reason");
    expect(required == 7680000UL, "complete-review-required-bytes");
    expect(OSMGAR3ModeReviewReasonString(reason)[0] == 'o', "reason-string");

    make_synthetic_8m_current_mode_review(&review);
    expect(OSMGAValidateR3ManualModeReview(&review, &required, &reason) == 1,
           "8m-current-mode-synthetic-review-accepted");
    expect(reason == OSMGA_R3_MODE_REVIEW_OK,
           "8m-current-mode-synthetic-review-reason");
    expect(required == 7680000UL,
           "8m-current-mode-synthetic-review-required-bytes");

    make_synthetic_8m_current_mode_review(&review);
    review.mapping_bytes = (8UL * 1024UL * 1024UL) + 1UL;
    expect(OSMGAValidateR3ManualModeReview(&review, &required, &reason) == 0,
           "8m-current-mode-mapping-vram-bound-enforced");
    expect(reason == OSMGA_R3_MODE_REVIEW_MAPPING_EXCEEDS_VRAM,
           "8m-current-mode-mapping-vram-bound-reason");

    make_synthetic_8m_current_mode_review(&review);
    review.mapping_bytes = 7679999UL;
    expect(OSMGAValidateR3ManualModeReview(&review, &required, &reason) == 0,
           "8m-current-mode-mapping-visible-bound-enforced");
    expect(reason == OSMGA_R3_MODE_REVIEW_MAPPING_TOO_SMALL,
           "8m-current-mode-mapping-visible-bound-reason");

    if (failures != 0) {
        return 1;
    }
    printf("OPENSTEP_MGA_R3_MODE_REVIEW_TEST_STATUS=pass\n");
    return 0;
}
