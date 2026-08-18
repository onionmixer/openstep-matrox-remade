/* Pure-C G1 snapshot admission policy; no target interfaces. */

#include "OpenStepMGARecoveryMatrix.h"

static int
snapshot_evidence_complete(const OSMGARecoverySnapshot *snapshot)
{
    return snapshot->bundle_verified && snapshot->instance_table_verified &&
           snapshot->rollback_instructions_verified;
}

int
OSMGAValidateRecoveryMatrix(const OSMGARecoveryMatrix *matrix,
                            OSMGARecoveryMatrixReason *reason)
{
    if (reason == 0) {
        return 0;
    }
    if (matrix == 0) {
        *reason = OSMGA_RECOVERY_MATRIX_INVALID_ARGUMENT;
        return 0;
    }
    if (matrix->original.original_candidate_count != 1 ||
        matrix->original.replacement_candidate_count != 0) {
        *reason = OSMGA_RECOVERY_MATRIX_ORIGINAL_OWNER;
        return 0;
    }
    if (matrix->recovery.original_candidate_count != 0 ||
        matrix->recovery.replacement_candidate_count != 1) {
        *reason = OSMGA_RECOVERY_MATRIX_RECOVERY_OWNER;
        return 0;
    }
    if (matrix->failure.original_candidate_count != 1 ||
        matrix->failure.replacement_candidate_count != 0) {
        *reason = OSMGA_RECOVERY_MATRIX_FAILURE_OWNER;
        return 0;
    }
    if (!snapshot_evidence_complete(&matrix->original) ||
        !snapshot_evidence_complete(&matrix->recovery) ||
        !snapshot_evidence_complete(&matrix->failure)) {
        *reason = OSMGA_RECOVERY_MATRIX_SNAPSHOT_EVIDENCE;
        return 0;
    }
    if (!matrix->atomic_install_verified) {
        *reason = OSMGA_RECOVERY_MATRIX_ATOMIC_INSTALL_UNVERIFIED;
        return 0;
    }
    if (!matrix->installer_rollback_verified) {
        *reason = OSMGA_RECOVERY_MATRIX_INSTALLER_ROLLBACK_UNVERIFIED;
        return 0;
    }
    if (!matrix->failure_original_boot_verified) {
        *reason = OSMGA_RECOVERY_MATRIX_FAILURE_ORIGINAL_BOOT_UNVERIFIED;
        return 0;
    }
    if (!matrix->independent_recovery_channel_verified) {
        *reason = OSMGA_RECOVERY_MATRIX_RECOVERY_CHANNEL_UNVERIFIED;
        return 0;
    }
    *reason = OSMGA_RECOVERY_MATRIX_OK;
    return 1;
}

const char *
OSMGARecoveryMatrixReasonString(OSMGARecoveryMatrixReason reason)
{
    switch (reason) {
    case OSMGA_RECOVERY_MATRIX_OK:
        return "ok";
    case OSMGA_RECOVERY_MATRIX_INVALID_ARGUMENT:
        return "invalid-argument";
    case OSMGA_RECOVERY_MATRIX_ORIGINAL_OWNER:
        return "original-owner";
    case OSMGA_RECOVERY_MATRIX_RECOVERY_OWNER:
        return "recovery-owner";
    case OSMGA_RECOVERY_MATRIX_FAILURE_OWNER:
        return "failure-owner";
    case OSMGA_RECOVERY_MATRIX_SNAPSHOT_EVIDENCE:
        return "snapshot-evidence";
    case OSMGA_RECOVERY_MATRIX_ATOMIC_INSTALL_UNVERIFIED:
        return "atomic-install-unverified";
    case OSMGA_RECOVERY_MATRIX_INSTALLER_ROLLBACK_UNVERIFIED:
        return "installer-rollback-unverified";
    case OSMGA_RECOVERY_MATRIX_FAILURE_ORIGINAL_BOOT_UNVERIFIED:
        return "failure-original-boot-unverified";
    case OSMGA_RECOVERY_MATRIX_RECOVERY_CHANNEL_UNVERIFIED:
        return "recovery-channel-unverified";
    }
    return "unknown";
}
