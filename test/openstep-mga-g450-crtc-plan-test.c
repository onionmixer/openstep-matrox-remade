#include <stdio.h>

#include "OpenStepMGAG450CRTCPlan.h"

static int failures;

static void
expect(int condition, const char *name)
{
    if (!condition) {
        printf("OPENSTEP_MGA_G450_CRTC_PLAN_TEST=fail:%s\n", name);
        failures++;
    }
}

static void
make_record(OSMGAR3ManualModeReview *record)
{
    record->physical_profile.evidence_mask =
        OSMGA_R2_EVIDENCE_BOARD_ID |
        OSMGA_R2_EVIDENCE_INDEPENDENT_CROSSCHECK |
        OSMGA_R2_EVIDENCE_VRAM_TYPE |
        OSMGA_R2_EVIDENCE_VRAM_SIZE |
        OSMGA_R2_EVIDENCE_RAMDAC_LIMIT;
    record->physical_profile.board_evidence_reference = "G450-PCI";
    record->physical_profile.crosscheck_evidence_reference = "operator-16MiB";
    record->physical_profile.vram_evidence_reference = "G450-16MiB";
    record->physical_profile.ramdac_evidence_reference = "G450-300MHz";
    record->physical_profile.vram_type = OSMGA_VRAM_TYPE_DDR_SDRAM;
    record->physical_profile.physical_vram_bytes = 16UL * 1024UL * 1024UL;
    record->physical_profile.applicable_ramdac_khz = 300000UL;
    record->configured_vram_bytes = 16UL * 1024UL * 1024UL;
    record->evidence_mask = OSMGA_R3_EVIDENCE_MODE_SOURCE |
                            OSMGA_R3_EVIDENCE_TIMING_SOURCE |
                            OSMGA_R3_EVIDENCE_PITCH_POLICY |
                            OSMGA_R3_EVIDENCE_MAPPING_BOUND;
    record->mode.width = 1600;
    record->mode.height = 1200;
    record->mode.refresh_millihz = 60000UL;
    record->timing.mode = record->mode;
    record->timing.pixel_clock_khz = 162000UL;
    record->timing.horizontal_front_porch = 64UL;
    record->timing.horizontal_sync = 192UL;
    record->timing.horizontal_back_porch = 304UL;
    record->timing.vertical_front_porch = 1UL;
    record->timing.vertical_sync = 3UL;
    record->timing.vertical_back_porch = 46UL;
    record->timing.hsync_positive = 1;
    record->timing.vsync_positive = 1;
    record->bits_per_pixel = 32;
    record->pitch_bytes = 6400UL;
    record->pitch_alignment_bytes = 8UL;
    record->pixel_clock_khz = 162000UL;
    record->mapping_bytes = 16UL * 1024UL * 1024UL;
}

int
main(void)
{
    OSMGAR3ManualModeReview record;
    OSMGAG450CRTCPlan plan;
    OSMGAG450CRTCPlanReason reason;

    make_record(&record);
    expect(OSMGABuildG450CRTCPlan(&record, &plan, &reason) == 1,
           "approved-record");
    expect(reason == OSMGA_G450_CRTC_PLAN_OK, "approved-reason");
    expect(plan.horizontal_sync_start == 1664UL, "horizontal-sync-start");
    expect(plan.horizontal_sync_end == 1856UL, "horizontal-sync-end");
    expect(plan.horizontal_total == 2160UL, "horizontal-total");
    expect(plan.vertical_sync_start == 1201UL, "vertical-sync-start");
    expect(plan.vertical_sync_end == 1204UL, "vertical-sync-end");
    expect(plan.vertical_total == 1250UL, "vertical-total");
    expect(plan.scanout_bytes == 7680000UL, "scanout");

    record.bits_per_pixel = 16;
    expect(OSMGABuildG450CRTCPlan(&record, &plan, &reason) == 0,
           "unsupported-format");
    expect(reason == OSMGA_G450_CRTC_PLAN_UNSUPPORTED_FORMAT,
           "unsupported-format-reason");

    if (failures != 0) {
        return 1;
    }
    printf("OPENSTEP_MGA_G450_CRTC_PLAN_TEST_STATUS=pass\n");
    return 0;
}
