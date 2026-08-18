/* Operator-approved first deployment record: PCI G450, 16 MiB, 1600x1200@60. */

#include <stdio.h>

#include "OpenStepMGAModeReview.h"

static int failures;

static void
expect(int condition, const char *name)
{
    if (!condition) {
        printf("OPENSTEP_MGA_G450_16M_MODE_RECORD_TEST=fail:%s\n", name);
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
    record->physical_profile.board_evidence_reference =
        "P0_TARGET_INVENTORY: PCI G450 topology";
    record->physical_profile.crosscheck_evidence_reference =
        "operator-confirmed G450 deployment profile 2026-08-18";
    record->physical_profile.vram_evidence_reference =
        "operator-confirmed 16MiB G450 minimum deployment limit";
    record->physical_profile.ramdac_evidence_reference =
        "original G450 16MiB catalogue DAC Speed=300MHz";
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
    OSMGAR3ModeReviewReason reason;
    unsigned long required_bytes;

    make_record(&record);
    expect(OSMGAValidateR3ManualModeReview(&record, &required_bytes, &reason) == 1,
           "approved-record-accepted");
    expect(reason == OSMGA_R3_MODE_REVIEW_OK, "approved-record-reason");
    expect(required_bytes == 7680000UL, "visible-footprint");
    expect(record.timing.pixel_clock_khz <=
           record.physical_profile.applicable_ramdac_khz,
           "clock-within-deployment-limit");

    record.physical_profile.physical_vram_bytes = 8UL * 1024UL * 1024UL;
    expect(OSMGAValidateR3ManualModeReview(&record, &required_bytes, &reason) == 0,
           "eight-mebibyte-rejected");
    expect(reason == OSMGA_R3_MODE_REVIEW_MANUAL_MEMORY_MISMATCH,
           "eight-mebibyte-reason");

    if (failures != 0) {
        return 1;
    }
    printf("OPENSTEP_MGA_G450_16M_MODE_RECORD_TEST_STATUS=pass\n");
    return 0;
}
