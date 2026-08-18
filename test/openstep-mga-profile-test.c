#include <stdio.h>

#include "OpenStepMGAProfile.h"

static int failures;

static void
expect(int condition, const char *name)
{
    if (!condition) {
        printf("OPENSTEP_MGA_PROFILE_TEST=fail:%s\n", name);
        failures++;
    }
}

int
main(void)
{
    OSMGAR2PhysicalProfile profile;
    OSMGAR2ProfileReason reason;
    unsigned long complete_mask;

    complete_mask = OSMGA_R2_EVIDENCE_BOARD_ID |
                    OSMGA_R2_EVIDENCE_INDEPENDENT_CROSSCHECK |
                    OSMGA_R2_EVIDENCE_VRAM_TYPE |
                    OSMGA_R2_EVIDENCE_VRAM_SIZE |
                    OSMGA_R2_EVIDENCE_RAMDAC_LIMIT;

    expect(OSMGAValidateR2PhysicalProfile(0, &reason) == 0,
           "null-profile-rejected");
    expect(reason == OSMGA_R2_PROFILE_INVALID_ARGUMENT,
           "null-profile-reason");
    expect(OSMGAValidateR2PhysicalProfile(0, 0) == 0,
           "null-arguments-rejected");

    profile.evidence_mask = 0;
    profile.board_evidence_reference = "B2";
    profile.crosscheck_evidence_reference = "B6";
    profile.vram_evidence_reference = "B4+B5";
    profile.ramdac_evidence_reference = "B5";
    profile.vram_type = OSMGA_VRAM_TYPE_DDR_SDRAM;
    profile.physical_vram_bytes = 16UL * 1024UL * 1024UL;
    profile.applicable_ramdac_khz = 300000UL;
    expect(OSMGAValidateR2PhysicalProfile(&profile, &reason) == 0,
           "board-id-evidence-required");
    expect(reason == OSMGA_R2_PROFILE_BOARD_ID_UNVERIFIED,
           "board-id-evidence-reason");

    profile.evidence_mask = OSMGA_R2_EVIDENCE_BOARD_ID;
    expect(OSMGAValidateR2PhysicalProfile(&profile, &reason) == 0,
           "crosscheck-evidence-required");
    expect(reason == OSMGA_R2_PROFILE_CROSSCHECK_UNVERIFIED,
           "crosscheck-evidence-reason");

    profile.evidence_mask = OSMGA_R2_EVIDENCE_BOARD_ID |
                            OSMGA_R2_EVIDENCE_INDEPENDENT_CROSSCHECK;
    expect(OSMGAValidateR2PhysicalProfile(&profile, &reason) == 0,
           "vram-type-evidence-required");
    expect(reason == OSMGA_R2_PROFILE_VRAM_TYPE_UNVERIFIED,
           "vram-type-evidence-reason");

    profile.evidence_mask = complete_mask;
    profile.board_evidence_reference = "";
    expect(OSMGAValidateR2PhysicalProfile(&profile, &reason) == 0,
           "board-reference-required");
    expect(reason == OSMGA_R2_PROFILE_BOARD_REFERENCE_MISSING,
           "board-reference-reason");
    profile.board_evidence_reference = "B2";
    profile.crosscheck_evidence_reference = 0;
    expect(OSMGAValidateR2PhysicalProfile(&profile, &reason) == 0,
           "crosscheck-reference-required");
    expect(reason == OSMGA_R2_PROFILE_CROSSCHECK_REFERENCE_MISSING,
           "crosscheck-reference-reason");
    profile.crosscheck_evidence_reference = "B6";
    profile.vram_evidence_reference = "";
    expect(OSMGAValidateR2PhysicalProfile(&profile, &reason) == 0,
           "vram-reference-required");
    expect(reason == OSMGA_R2_PROFILE_VRAM_REFERENCE_MISSING,
           "vram-reference-reason");
    profile.vram_evidence_reference = "B4+B5";
    profile.ramdac_evidence_reference = "";
    expect(OSMGAValidateR2PhysicalProfile(&profile, &reason) == 0,
           "ramdac-reference-required");
    expect(reason == OSMGA_R2_PROFILE_RAMDAC_REFERENCE_MISSING,
           "ramdac-reference-reason");
    profile.ramdac_evidence_reference = "B5";
    profile.vram_type = OSMGA_VRAM_TYPE_UNKNOWN;
    expect(OSMGAValidateR2PhysicalProfile(&profile, &reason) == 0,
           "unknown-vram-type-rejected");
    expect(reason == OSMGA_R2_PROFILE_VRAM_TYPE_INVALID,
           "unknown-vram-type-reason");

    profile.vram_type = OSMGA_VRAM_TYPE_DDR_SDRAM;
    profile.physical_vram_bytes = 0;
    expect(OSMGAValidateR2PhysicalProfile(&profile, &reason) == 0,
           "zero-vram-size-rejected");
    expect(reason == OSMGA_R2_PROFILE_VRAM_SIZE_INVALID,
           "zero-vram-size-reason");

    profile.physical_vram_bytes = 16UL * 1024UL * 1024UL;
    profile.applicable_ramdac_khz = 0;
    expect(OSMGAValidateR2PhysicalProfile(&profile, &reason) == 0,
           "zero-ramdac-rejected");
    expect(reason == OSMGA_R2_PROFILE_RAMDAC_INVALID,
           "zero-ramdac-reason");

    profile.applicable_ramdac_khz = 300000UL;
    expect(OSMGAValidateR2PhysicalProfile(&profile, &reason) == 1,
           "complete-evidence-profile-accepted");
    expect(reason == OSMGA_R2_PROFILE_OK, "complete-evidence-reason");
    expect(OSMGAR2ProfileReasonString(reason)[0] == 'o', "reason-string");

    if (failures != 0) {
        return 1;
    }
    printf("OPENSTEP_MGA_PROFILE_TEST_STATUS=pass\n");
    return 0;
}
