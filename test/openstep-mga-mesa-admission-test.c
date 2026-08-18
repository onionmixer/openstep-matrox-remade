#include <stdio.h>

#include "OpenStepMGAMesaAdmission.h"

static int failures;

static void
expect(int condition, const char *name)
{
    if (!condition) {
        printf("OPENSTEP_MGA_MESA_ADMISSION_TEST=fail:%s\n", name);
        failures++;
    }
}

static void
make_mapping_review(OSMGAR6MappingReview *mapping)
{
    OSMGAR3ManualModeReview *mode;

    mode = &mapping->mode_review;
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

    mapping->sole_owner_snapshot_verified = 1;
    mapping->recovery_path_verified = 1;
    mapping->range_list_verified = 1;
    mapping->cache_policy_verified = 1;
    mapping->memory_range_count = 2;
    mapping->framebuffer_range_index = 0;
    mapping->framebuffer_range_bytes = 16UL * 1024UL * 1024UL;
    mapping->cache_policy = OSMGA_CACHE_POLICY_WRITE_THROUGH;
}

static void
make_budget(OSMGARenderBudgetRequest *budget)
{
    budget->available_bytes = 16UL * 1024UL * 1024UL;
    budget->scanout_bytes = 1600UL * 1200UL * 4UL;
    budget->reserved_bytes = 0;
    budget->render_width = OSMGA_MESA_RENDER_WIDTH;
    budget->render_height = OSMGA_MESA_RENDER_HEIGHT;
    budget->color_bits_per_pixel = OSMGA_MESA_COLOR_BITS;
    budget->depth_bits_per_pixel = OSMGA_MESA_DEPTH_BITS;
    budget->color_buffer_count = 2;
    budget->pitch_alignment_bytes = 8UL;
}

int
main(void)
{
    OSMGAR6MappingReview mapping;
    OSMGARenderBudgetRequest budget;
    OSMGARenderBudgetResult result;
    OSMGAMesaBackendRequest backend;
    OSMGAMesaAdmissionReason admission_reason;
    OSMGAMesaBackendDecision decision;
    OSMGAMesaBackendReason backend_reason;

    make_mapping_review(&mapping);
    make_budget(&budget);
    expect(OSMGABuildMesaBackendRequest(&mapping, &budget, 1, 0, 1,
                                        &backend, &result,
                                        &admission_reason) == 1,
           "complete-synthetic-review");
    expect(admission_reason == OSMGA_MESA_ADMISSION_OK,
           "complete-synthetic-reason");
    expect(result.total_used_bytes == 15544320UL,
           "sixteen-mebibyte-total");
    expect(result.remaining_bytes == 1232896UL,
           "sixteen-mebibyte-remaining");
    expect(OSMGASelectMesaBackend(&backend, &decision, &backend_reason) == 1,
           "fence-missing-fallback");
    expect(decision == OSMGA_MESA_BACKEND_SOFTWARE_FALLBACK,
           "fence-missing-fallback-decision");

    expect(OSMGABuildMesaBackendRequest(&mapping, &budget, 1, 1, 1,
                                        &backend, &result,
                                        &admission_reason) == 1,
           "fence-attested-review");
    expect(OSMGASelectMesaBackend(&backend, &decision, &backend_reason) == 1,
           "synthetic-candidate-selected");
    expect(decision == OSMGA_MESA_BACKEND_HARDWARE_CANDIDATE,
           "synthetic-candidate-decision");

    make_budget(&budget);
    budget.available_bytes = 8UL * 1024UL * 1024UL;
    expect(OSMGABuildMesaBackendRequest(&mapping, &budget, 1, 1, 1,
                                        &backend, &result,
                                        &admission_reason) == 0,
           "profile-budget-mismatch-rejected");
    expect(admission_reason == OSMGA_MESA_ADMISSION_PROFILE_BUDGET_MISMATCH,
           "profile-budget-mismatch-reason");

    make_budget(&budget);
    budget.scanout_bytes--;
    expect(OSMGABuildMesaBackendRequest(&mapping, &budget, 1, 1, 1,
                                        &backend, &result,
                                        &admission_reason) == 0,
           "scanout-mismatch-rejected");
    expect(admission_reason == OSMGA_MESA_ADMISSION_SCANOUT_BUDGET_MISMATCH,
           "scanout-mismatch-reason");

    expect(OSMGAMesaAdmissionReasonString(OSMGA_MESA_ADMISSION_OK)[0] == 'o',
           "reason-string");
    if (failures != 0) {
        return 1;
    }
    printf("OPENSTEP_MGA_MESA_ADMISSION_TEST_STATUS=pass\n");
    return 0;
}
