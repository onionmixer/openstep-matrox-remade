/*
 * OpenStepMGAProfile.h - pure-C admission policy for R2 physical evidence.
 *
 * This type does not discover or program hardware.  It rejects a profile
 * until every value has the required evidence category.  In particular, a
 * PCI catalogue label or an existing display configuration is never a
 * substitute for physical VRAM evidence.
 */

#ifndef OPENSTEP_MGA_PROFILE_H
#define OPENSTEP_MGA_PROFILE_H

typedef enum {
    OSMGA_VRAM_TYPE_UNKNOWN = 0,
    OSMGA_VRAM_TYPE_SDR_SDRAM,
    OSMGA_VRAM_TYPE_DDR_SDRAM
} OSMGAVRAMType;

enum {
    OSMGA_R2_EVIDENCE_BOARD_ID = 1UL << 0,
    OSMGA_R2_EVIDENCE_INDEPENDENT_CROSSCHECK = 1UL << 1,
    OSMGA_R2_EVIDENCE_VRAM_TYPE = 1UL << 2,
    OSMGA_R2_EVIDENCE_VRAM_SIZE = 1UL << 3,
    OSMGA_R2_EVIDENCE_RAMDAC_LIMIT = 1UL << 4
};

typedef struct {
    unsigned long evidence_mask;
    const char *board_evidence_reference;
    const char *crosscheck_evidence_reference;
    const char *vram_evidence_reference;
    const char *ramdac_evidence_reference;
    OSMGAVRAMType vram_type;
    unsigned long physical_vram_bytes;
    unsigned long applicable_ramdac_khz;
} OSMGAR2PhysicalProfile;

typedef enum {
    OSMGA_R2_PROFILE_OK = 0,
    OSMGA_R2_PROFILE_INVALID_ARGUMENT,
    OSMGA_R2_PROFILE_BOARD_ID_UNVERIFIED,
    OSMGA_R2_PROFILE_BOARD_REFERENCE_MISSING,
    OSMGA_R2_PROFILE_CROSSCHECK_UNVERIFIED,
    OSMGA_R2_PROFILE_CROSSCHECK_REFERENCE_MISSING,
    OSMGA_R2_PROFILE_VRAM_TYPE_UNVERIFIED,
    OSMGA_R2_PROFILE_VRAM_REFERENCE_MISSING,
    OSMGA_R2_PROFILE_VRAM_TYPE_INVALID,
    OSMGA_R2_PROFILE_VRAM_SIZE_UNVERIFIED,
    OSMGA_R2_PROFILE_VRAM_SIZE_INVALID,
    OSMGA_R2_PROFILE_RAMDAC_UNVERIFIED,
    OSMGA_R2_PROFILE_RAMDAC_REFERENCE_MISSING,
    OSMGA_R2_PROFILE_RAMDAC_INVALID
} OSMGAR2ProfileReason;

/*
 * Return 1 only for a non-inferred physical board profile.  This is a policy
 * check, not a probe: it does not inspect PCI, memory, DAC, or a driver.
 * `reason` is written on every call when non-null.  Passing this check does
 * not authorize mapping, mode programming, or P3 work.
 */
int OSMGAValidateR2PhysicalProfile(const OSMGAR2PhysicalProfile *profile,
                                   OSMGAR2ProfileReason *reason);

const char *OSMGAR2ProfileReasonString(OSMGAR2ProfileReason reason);

#endif /* OPENSTEP_MGA_PROFILE_H */
