#include <stdio.h>

#include "OpenStepMGAMappingReview.h"

static int failures;

static void
expect(int condition, const char *name)
{
    if (!condition) {
        printf("OPENSTEP_MGA_R6_MAPPING_REVIEW_TEST=fail:%s\n", name);
        failures++;
    }
}

static void
make_complete_review(OSMGAR6MappingReview *review)
{
    review->mode_review.physical_profile.evidence_mask =
        OSMGA_R2_EVIDENCE_BOARD_ID |
        OSMGA_R2_EVIDENCE_INDEPENDENT_CROSSCHECK |
        OSMGA_R2_EVIDENCE_VRAM_TYPE |
        OSMGA_R2_EVIDENCE_VRAM_SIZE |
        OSMGA_R2_EVIDENCE_RAMDAC_LIMIT;
    review->mode_review.physical_profile.board_evidence_reference = "B2";
    review->mode_review.physical_profile.crosscheck_evidence_reference = "B6";
    review->mode_review.physical_profile.vram_evidence_reference = "B4+B5";
    review->mode_review.physical_profile.ramdac_evidence_reference = "B5";
    review->mode_review.physical_profile.vram_type = OSMGA_VRAM_TYPE_DDR_SDRAM;
    review->mode_review.physical_profile.physical_vram_bytes =
        16UL * 1024UL * 1024UL;
    review->mode_review.physical_profile.applicable_ramdac_khz = 300000UL;
    review->mode_review.configured_vram_bytes = 16UL * 1024UL * 1024UL;
    review->mode_review.evidence_mask =
        OSMGA_R3_EVIDENCE_MODE_SOURCE |
        OSMGA_R3_EVIDENCE_TIMING_SOURCE |
        OSMGA_R3_EVIDENCE_PITCH_POLICY |
        OSMGA_R3_EVIDENCE_MAPPING_BOUND;
    review->mode_review.mode.width = 1600;
    review->mode_review.mode.height = 1200;
    review->mode_review.mode.refresh_millihz = 60000UL;
    review->mode_review.timing.mode = review->mode_review.mode;
    review->mode_review.timing.pixel_clock_khz = 162000UL;
    review->mode_review.timing.horizontal_front_porch = 64UL;
    review->mode_review.timing.horizontal_sync = 192UL;
    review->mode_review.timing.horizontal_back_porch = 304UL;
    review->mode_review.timing.vertical_front_porch = 1UL;
    review->mode_review.timing.vertical_sync = 3UL;
    review->mode_review.timing.vertical_back_porch = 46UL;
    review->mode_review.timing.hsync_positive = 1;
    review->mode_review.timing.vsync_positive = 1;
    review->mode_review.bits_per_pixel = 32;
    review->mode_review.pitch_bytes = 6400UL;
    review->mode_review.pitch_alignment_bytes = 8UL;
    review->mode_review.pixel_clock_khz = 162000UL;
    review->mode_review.mapping_bytes = 16UL * 1024UL * 1024UL;
    review->sole_owner_snapshot_verified = 1;
    review->recovery_path_verified = 1;
    review->range_list_verified = 1;
    review->cache_policy_verified = 1;
    review->memory_range_count = 2;
    review->framebuffer_range_index = 0;
    review->framebuffer_range_bytes = 16UL * 1024UL * 1024UL;
    review->cache_policy = OSMGA_CACHE_POLICY_WRITE_THROUGH;
}

int
main(void)
{
    OSMGAR6MappingReview review;
    OSMGAR6MappingReviewReason reason;

    expect(OSMGAValidateR6MappingReview(0, &reason) == 0,
           "null-review-rejected");
    expect(reason == OSMGA_R6_MAPPING_REVIEW_INVALID_ARGUMENT,
           "null-review-reason");

    make_complete_review(&review);
    review.sole_owner_snapshot_verified = 0;
    expect(OSMGAValidateR6MappingReview(&review, &reason) == 0,
           "sole-owner-required");
    expect(reason == OSMGA_R6_MAPPING_REVIEW_SOLE_OWNER_UNVERIFIED,
           "sole-owner-reason");

    make_complete_review(&review);
    review.recovery_path_verified = 0;
    expect(OSMGAValidateR6MappingReview(&review, &reason) == 0,
           "recovery-path-required");
    expect(reason == OSMGA_R6_MAPPING_REVIEW_RECOVERY_UNVERIFIED,
           "recovery-path-reason");

    make_complete_review(&review);
    review.range_list_verified = 0;
    expect(OSMGAValidateR6MappingReview(&review, &reason) == 0,
           "range-evidence-required");
    expect(reason == OSMGA_R6_MAPPING_REVIEW_RANGE_LIST_UNVERIFIED,
           "range-evidence-reason");

    make_complete_review(&review);
    review.cache_policy_verified = 0;
    expect(OSMGAValidateR6MappingReview(&review, &reason) == 0,
           "cache-evidence-required");
    expect(reason == OSMGA_R6_MAPPING_REVIEW_CACHE_POLICY_UNVERIFIED,
           "cache-evidence-reason");

    make_complete_review(&review);
    review.framebuffer_range_index = 2;
    expect(OSMGAValidateR6MappingReview(&review, &reason) == 0,
           "range-index-bounded");
    expect(reason == OSMGA_R6_MAPPING_REVIEW_RANGE_INDEX_INVALID,
           "range-index-reason");

    make_complete_review(&review);
    review.framebuffer_range_bytes = 7679999UL;
    expect(OSMGAValidateR6MappingReview(&review, &reason) == 0,
           "range-size-enforced");
    expect(reason == OSMGA_R6_MAPPING_REVIEW_RANGE_TOO_SMALL,
           "range-size-reason");

    make_complete_review(&review);
    review.cache_policy = OSMGA_CACHE_POLICY_UNSPECIFIED;
    expect(OSMGAValidateR6MappingReview(&review, &reason) == 0,
           "cache-policy-enforced");
    expect(reason == OSMGA_R6_MAPPING_REVIEW_CACHE_POLICY_INVALID,
           "cache-policy-reason");

    make_complete_review(&review);
    expect(OSMGAValidateR6MappingReview(&review, &reason) == 1,
           "complete-synthetic-review-accepted");
    expect(reason == OSMGA_R6_MAPPING_REVIEW_OK, "complete-review-reason");
    expect(OSMGAR6MappingReviewReasonString(reason)[0] == 'o', "reason-string");

    if (failures != 0) {
        return 1;
    }
    printf("OPENSTEP_MGA_R6_MAPPING_REVIEW_TEST_STATUS=pass\n");
    return 0;
}
