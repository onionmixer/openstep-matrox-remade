/*
 * OpenStepMGARecoveryMatrix.h - offline G1 snapshot admission policy.
 *
 * Candidate counts are reviewed configuration facts, never live probes.
 */

#ifndef OPENSTEP_MGA_RECOVERY_MATRIX_H
#define OPENSTEP_MGA_RECOVERY_MATRIX_H

typedef struct {
    unsigned int original_candidate_count;
    unsigned int replacement_candidate_count;
    int bundle_verified;
    int instance_table_verified;
    int rollback_instructions_verified;
} OSMGARecoverySnapshot;

typedef struct {
    OSMGARecoverySnapshot original;
    OSMGARecoverySnapshot recovery;
    OSMGARecoverySnapshot failure;
    int atomic_install_verified;
    int installer_rollback_verified;
    int failure_original_boot_verified;
    int independent_recovery_channel_verified;
} OSMGARecoveryMatrix;

typedef enum {
    OSMGA_RECOVERY_MATRIX_OK = 0,
    OSMGA_RECOVERY_MATRIX_INVALID_ARGUMENT,
    OSMGA_RECOVERY_MATRIX_ORIGINAL_OWNER,
    OSMGA_RECOVERY_MATRIX_RECOVERY_OWNER,
    OSMGA_RECOVERY_MATRIX_FAILURE_OWNER,
    OSMGA_RECOVERY_MATRIX_SNAPSHOT_EVIDENCE,
    OSMGA_RECOVERY_MATRIX_ATOMIC_INSTALL_UNVERIFIED,
    OSMGA_RECOVERY_MATRIX_INSTALLER_ROLLBACK_UNVERIFIED,
    OSMGA_RECOVERY_MATRIX_FAILURE_ORIGINAL_BOOT_UNVERIFIED,
    OSMGA_RECOVERY_MATRIX_RECOVERY_CHANNEL_UNVERIFIED
} OSMGARecoveryMatrixReason;

/*
 * Require P-original and P-failure to select original-only (1/0), and
 * P-recovery to select replacement-only (0/1).  This validates records only;
 * it does not inspect DriverLoader, alter Configure, load a bundle, or reboot.
 */
int OSMGAValidateRecoveryMatrix(const OSMGARecoveryMatrix *matrix,
                                OSMGARecoveryMatrixReason *reason);

const char *OSMGARecoveryMatrixReasonString(OSMGARecoveryMatrixReason reason);

#endif /* OPENSTEP_MGA_RECOVERY_MATRIX_H */
