#include <stdio.h>

#include "OpenStepMGAG450PLL.h"

static int failures;

static void
expect(int condition, const char *name)
{
    if (!condition) {
        printf("OPENSTEP_MGA_G450_PLL_TEST=fail:%s\n", name);
        failures++;
    }
}

static void
make_complete_review(OSMGAG450PLLReview *review)
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
    review->pll_source_verified = 1;
    review->head_selection_verified = 1;
    review->head = OSMGA_G450_HEAD_PRIMARY;
}

int
main(void)
{
    OSMGAG450PLLReview review;
    OSMGAG450PLLPlan plan;
    OSMGAG450PLLReason reason;

    expect(OSMGAPlanG450PixelPLL(0, &plan, &reason) == 0,
           "null-review-rejected");
    expect(reason == OSMGA_G450_PLL_INVALID_ARGUMENT, "null-review-reason");

    make_complete_review(&review);
    review.pll_source_verified = 0;
    expect(OSMGAPlanG450PixelPLL(&review, &plan, &reason) == 0,
           "source-evidence-required");
    expect(reason == OSMGA_G450_PLL_SOURCE_UNVERIFIED,
           "source-evidence-reason");

    make_complete_review(&review);
    review.head_selection_verified = 0;
    expect(OSMGAPlanG450PixelPLL(&review, &plan, &reason) == 0,
           "head-evidence-required");
    expect(reason == OSMGA_G450_PLL_HEAD_UNVERIFIED,
           "head-evidence-reason");

    make_complete_review(&review);
    review.head = OSMGA_G450_HEAD_UNKNOWN;
    expect(OSMGAPlanG450PixelPLL(&review, &plan, &reason) == 0,
           "head-value-required");
    expect(reason == OSMGA_G450_PLL_HEAD_INVALID, "head-value-reason");

    make_complete_review(&review);
    expect(OSMGAPlanG450PixelPLL(&review, &plan, &reason) == 1,
           "complete-synthetic-review-accepted");
    expect(reason == OSMGA_G450_PLL_OK, "complete-review-reason");
    expect(plan.requested_khz == 162000UL, "requested-clock-recorded");
    expect(plan.achieved_khz >= 161000UL && plan.achieved_khz <= 163000UL,
           "candidate-close-to-request");
    expect(plan.vco_khz >= 256000UL && plan.vco_khz <= 1300000UL,
           "vco-bounds");
    expect(plan.post_divider == 2U || plan.post_divider == 4U ||
           plan.post_divider == 8U || plan.post_divider == 16U,
           "post-divider-bound");
    expect(OSMGAG450PLLReasonString(reason)[0] == 'o', "reason-string");

    if (failures != 0) {
        return 1;
    }
    printf("OPENSTEP_MGA_G450_PLL_TEST_STATUS=pass\n");
    return 0;
}
