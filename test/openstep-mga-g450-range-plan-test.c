#include <stdio.h>

#include "OpenStepMGAG450RangePlan.h"

static int failures;

static void
expect(int condition, const char *name)
{
    if (!condition) {
        printf("OPENSTEP_MGA_G450_RANGE_PLAN_TEST=fail:%s\n", name);
        failures++;
    }
}

static void
make_review(OSMGAR6MappingReview *review)
{
    OSMGAR3ManualModeReview *mode;

    mode = &review->mode_review;
    mode->physical_profile.evidence_mask =
        OSMGA_R2_EVIDENCE_BOARD_ID |
        OSMGA_R2_EVIDENCE_INDEPENDENT_CROSSCHECK |
        OSMGA_R2_EVIDENCE_VRAM_TYPE |
        OSMGA_R2_EVIDENCE_VRAM_SIZE |
        OSMGA_R2_EVIDENCE_RAMDAC_LIMIT;
    mode->physical_profile.board_evidence_reference = "B2";
    mode->physical_profile.crosscheck_evidence_reference = "B6";
    mode->physical_profile.vram_evidence_reference = "B4+B5";
    mode->physical_profile.ramdac_evidence_reference = "B5";
    mode->physical_profile.vram_type = OSMGA_VRAM_TYPE_DDR_SDRAM;
    mode->physical_profile.physical_vram_bytes = 16UL * 1024UL * 1024UL;
    mode->physical_profile.applicable_ramdac_khz = 300000UL;
    mode->configured_vram_bytes = 16UL * 1024UL * 1024UL;
    mode->evidence_mask = OSMGA_R3_EVIDENCE_MODE_SOURCE |
                          OSMGA_R3_EVIDENCE_TIMING_SOURCE |
                          OSMGA_R3_EVIDENCE_PITCH_POLICY |
                          OSMGA_R3_EVIDENCE_MAPPING_BOUND;
    mode->mode.width = 1600;
    mode->mode.height = 1200;
    mode->mode.refresh_millihz = 60000UL;
    mode->timing.mode = mode->mode;
    mode->timing.pixel_clock_khz = 162000UL;
    mode->timing.horizontal_front_porch = 64UL;
    mode->timing.horizontal_sync = 192UL;
    mode->timing.horizontal_back_porch = 304UL;
    mode->timing.vertical_front_porch = 1UL;
    mode->timing.vertical_sync = 3UL;
    mode->timing.vertical_back_porch = 46UL;
    mode->timing.hsync_positive = 1;
    mode->timing.vsync_positive = 1;
    mode->bits_per_pixel = 32;
    mode->pitch_bytes = 6400UL;
    mode->pitch_alignment_bytes = 8UL;
    mode->pixel_clock_khz = 162000UL;
    mode->mapping_bytes = 16UL * 1024UL * 1024UL;
    review->sole_owner_snapshot_verified = 1;
    review->recovery_path_verified = 1;
    review->range_list_verified = 1;
    review->cache_policy_verified = 1;
    review->memory_range_count = 3U;
    review->framebuffer_range_index = 0U;
    review->framebuffer_range_bytes = 16UL * 1024UL * 1024UL;
    review->cache_policy = OSMGA_CACHE_POLICY_WRITE_THROUGH;
}

int
main(void)
{
    OSMGAR6MappingReview review;
    OSMGAG450RangePlan plan;
    OSMGAG450RangePlanReason reason;

    make_review(&review);
    expect(OSMGABuildG450LegacyRangePlan(&review, 0xf0000000UL, &plan,
                                         &reason) == 1,
           "complete-plan");
    expect(reason == OSMGA_G450_RANGE_PLAN_OK, "complete-reason");
    expect(plan.framebuffer_index == 0U && plan.vga_index == 1U &&
               plan.bios_index == 2U,
           "fixed-indices");
    expect(plan.ranges[0].physical_start == 0xf0000000UL &&
               plan.ranges[0].length_bytes == 16UL * 1024UL * 1024UL,
           "framebuffer-range");
    expect(plan.ranges[1].physical_start == 0x000a0000UL &&
               plan.ranges[1].length_bytes == 0x00020000UL,
           "vga-range");
    expect(plan.ranges[2].physical_start == 0x000c0000UL &&
               plan.ranges[2].length_bytes == 0x00010000UL,
           "bios-range");

    make_review(&review);
    expect(OSMGABuildG450LegacyRangePlan(&review, 0xf0000001UL, &plan,
                                         &reason) == 0,
           "unaligned-base-rejected");
    expect(reason == OSMGA_G450_RANGE_PLAN_FRAMEBUFFER_START,
           "unaligned-base-reason");

    make_review(&review);
    review.framebuffer_range_bytes = 8UL * 1024UL * 1024UL;
    expect(OSMGABuildG450LegacyRangePlan(&review, 0xf0000000UL, &plan,
                                         &reason) == 0,
           "wrong-length-rejected");
    expect(reason == OSMGA_G450_RANGE_PLAN_FRAMEBUFFER_LENGTH,
           "wrong-length-reason");

    make_review(&review);
    review.memory_range_count = 2U;
    expect(OSMGABuildG450LegacyRangePlan(&review, 0xf0000000UL, &plan,
                                         &reason) == 0,
           "wrong-layout-rejected");
    expect(reason == OSMGA_G450_RANGE_PLAN_RANGE_LAYOUT,
           "wrong-layout-reason");

    expect(OSMGAG450RangePlanReasonString(OSMGA_G450_RANGE_PLAN_OK)[0] == 'o',
           "reason-string");
    if (failures != 0) {
        return 1;
    }
    printf("OPENSTEP_MGA_G450_RANGE_PLAN_TEST_STATUS=pass\n");
    return 0;
}
