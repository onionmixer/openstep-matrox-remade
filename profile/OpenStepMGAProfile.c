/* Pure-C R2 physical-profile admission policy; no target interfaces. */

#include "OpenStepMGAProfile.h"

static int
has_reference(const char *value)
{
    return value != 0 && value[0] != '\0';
}

int
OSMGAValidateR2PhysicalProfile(const OSMGAR2PhysicalProfile *profile,
                               OSMGAR2ProfileReason *reason)
{
    if (reason == 0) {
        return 0;
    }
    if (profile == 0) {
        *reason = OSMGA_R2_PROFILE_INVALID_ARGUMENT;
        return 0;
    }
    if ((profile->evidence_mask & OSMGA_R2_EVIDENCE_BOARD_ID) == 0) {
        *reason = OSMGA_R2_PROFILE_BOARD_ID_UNVERIFIED;
        return 0;
    }
    if (!has_reference(profile->board_evidence_reference)) {
        *reason = OSMGA_R2_PROFILE_BOARD_REFERENCE_MISSING;
        return 0;
    }
    if ((profile->evidence_mask & OSMGA_R2_EVIDENCE_INDEPENDENT_CROSSCHECK) == 0) {
        *reason = OSMGA_R2_PROFILE_CROSSCHECK_UNVERIFIED;
        return 0;
    }
    if (!has_reference(profile->crosscheck_evidence_reference)) {
        *reason = OSMGA_R2_PROFILE_CROSSCHECK_REFERENCE_MISSING;
        return 0;
    }
    if ((profile->evidence_mask & OSMGA_R2_EVIDENCE_VRAM_TYPE) == 0) {
        *reason = OSMGA_R2_PROFILE_VRAM_TYPE_UNVERIFIED;
        return 0;
    }
    if (!has_reference(profile->vram_evidence_reference)) {
        *reason = OSMGA_R2_PROFILE_VRAM_REFERENCE_MISSING;
        return 0;
    }
    if (profile->vram_type != OSMGA_VRAM_TYPE_SDR_SDRAM &&
        profile->vram_type != OSMGA_VRAM_TYPE_DDR_SDRAM) {
        *reason = OSMGA_R2_PROFILE_VRAM_TYPE_INVALID;
        return 0;
    }
    if ((profile->evidence_mask & OSMGA_R2_EVIDENCE_VRAM_SIZE) == 0) {
        *reason = OSMGA_R2_PROFILE_VRAM_SIZE_UNVERIFIED;
        return 0;
    }
    if (profile->physical_vram_bytes == 0) {
        *reason = OSMGA_R2_PROFILE_VRAM_SIZE_INVALID;
        return 0;
    }
    if ((profile->evidence_mask & OSMGA_R2_EVIDENCE_RAMDAC_LIMIT) == 0) {
        *reason = OSMGA_R2_PROFILE_RAMDAC_UNVERIFIED;
        return 0;
    }
    if (!has_reference(profile->ramdac_evidence_reference)) {
        *reason = OSMGA_R2_PROFILE_RAMDAC_REFERENCE_MISSING;
        return 0;
    }
    if (profile->applicable_ramdac_khz == 0) {
        *reason = OSMGA_R2_PROFILE_RAMDAC_INVALID;
        return 0;
    }
    *reason = OSMGA_R2_PROFILE_OK;
    return 1;
}

const char *
OSMGAR2ProfileReasonString(OSMGAR2ProfileReason reason)
{
    switch (reason) {
    case OSMGA_R2_PROFILE_OK:
        return "ok";
    case OSMGA_R2_PROFILE_INVALID_ARGUMENT:
        return "invalid-argument";
    case OSMGA_R2_PROFILE_BOARD_ID_UNVERIFIED:
        return "board-id-unverified";
    case OSMGA_R2_PROFILE_BOARD_REFERENCE_MISSING:
        return "board-reference-missing";
    case OSMGA_R2_PROFILE_CROSSCHECK_UNVERIFIED:
        return "crosscheck-unverified";
    case OSMGA_R2_PROFILE_CROSSCHECK_REFERENCE_MISSING:
        return "crosscheck-reference-missing";
    case OSMGA_R2_PROFILE_VRAM_TYPE_UNVERIFIED:
        return "vram-type-unverified";
    case OSMGA_R2_PROFILE_VRAM_REFERENCE_MISSING:
        return "vram-reference-missing";
    case OSMGA_R2_PROFILE_VRAM_TYPE_INVALID:
        return "vram-type-invalid";
    case OSMGA_R2_PROFILE_VRAM_SIZE_UNVERIFIED:
        return "vram-size-unverified";
    case OSMGA_R2_PROFILE_VRAM_SIZE_INVALID:
        return "vram-size-invalid";
    case OSMGA_R2_PROFILE_RAMDAC_UNVERIFIED:
        return "ramdac-unverified";
    case OSMGA_R2_PROFILE_RAMDAC_REFERENCE_MISSING:
        return "ramdac-reference-missing";
    case OSMGA_R2_PROFILE_RAMDAC_INVALID:
        return "ramdac-invalid";
    }
    return "unknown";
}
