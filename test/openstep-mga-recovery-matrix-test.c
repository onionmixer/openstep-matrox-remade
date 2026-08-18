#include <stdio.h>

#include "OpenStepMGARecoveryMatrix.h"

static int failures;

static void
expect(int condition, const char *name)
{
    if (!condition) {
        printf("OPENSTEP_MGA_RECOVERY_MATRIX_TEST=fail:%s\n", name);
        failures++;
    }
}

static void
make_snapshot(OSMGARecoverySnapshot *snapshot, unsigned int original,
              unsigned int replacement)
{
    snapshot->original_candidate_count = original;
    snapshot->replacement_candidate_count = replacement;
    snapshot->bundle_verified = 1;
    snapshot->instance_table_verified = 1;
    snapshot->rollback_instructions_verified = 1;
}

static void
make_complete_matrix(OSMGARecoveryMatrix *matrix)
{
    make_snapshot(&matrix->original, 1, 0);
    make_snapshot(&matrix->recovery, 0, 1);
    make_snapshot(&matrix->failure, 1, 0);
    matrix->atomic_install_verified = 1;
    matrix->installer_rollback_verified = 1;
    matrix->failure_original_boot_verified = 1;
    matrix->independent_recovery_channel_verified = 1;
}

int
main(void)
{
    OSMGARecoveryMatrix matrix;
    OSMGARecoveryMatrixReason reason;

    expect(OSMGAValidateRecoveryMatrix(0, &reason) == 0,
           "null-matrix-rejected");
    expect(reason == OSMGA_RECOVERY_MATRIX_INVALID_ARGUMENT,
           "null-matrix-reason");

    make_complete_matrix(&matrix);
    matrix.original.replacement_candidate_count = 1;
    expect(OSMGAValidateRecoveryMatrix(&matrix, &reason) == 0,
           "original-dual-owner-rejected");
    expect(reason == OSMGA_RECOVERY_MATRIX_ORIGINAL_OWNER,
           "original-owner-reason");

    make_complete_matrix(&matrix);
    matrix.recovery.original_candidate_count = 1;
    expect(OSMGAValidateRecoveryMatrix(&matrix, &reason) == 0,
           "recovery-dual-owner-rejected");
    expect(reason == OSMGA_RECOVERY_MATRIX_RECOVERY_OWNER,
           "recovery-owner-reason");

    make_complete_matrix(&matrix);
    matrix.failure.replacement_candidate_count = 1;
    expect(OSMGAValidateRecoveryMatrix(&matrix, &reason) == 0,
           "failure-dual-owner-rejected");
    expect(reason == OSMGA_RECOVERY_MATRIX_FAILURE_OWNER,
           "failure-owner-reason");

    make_complete_matrix(&matrix);
    matrix.recovery.instance_table_verified = 0;
    expect(OSMGAValidateRecoveryMatrix(&matrix, &reason) == 0,
           "snapshot-evidence-required");
    expect(reason == OSMGA_RECOVERY_MATRIX_SNAPSHOT_EVIDENCE,
           "snapshot-evidence-reason");

    make_complete_matrix(&matrix);
    matrix.atomic_install_verified = 0;
    expect(OSMGAValidateRecoveryMatrix(&matrix, &reason) == 0,
           "atomic-install-required");
    expect(reason == OSMGA_RECOVERY_MATRIX_ATOMIC_INSTALL_UNVERIFIED,
           "atomic-install-reason");

    make_complete_matrix(&matrix);
    matrix.installer_rollback_verified = 0;
    expect(OSMGAValidateRecoveryMatrix(&matrix, &reason) == 0,
           "installer-rollback-required");
    expect(reason == OSMGA_RECOVERY_MATRIX_INSTALLER_ROLLBACK_UNVERIFIED,
           "installer-rollback-reason");

    make_complete_matrix(&matrix);
    matrix.failure_original_boot_verified = 0;
    expect(OSMGAValidateRecoveryMatrix(&matrix, &reason) == 0,
           "failure-original-boot-required");
    expect(reason == OSMGA_RECOVERY_MATRIX_FAILURE_ORIGINAL_BOOT_UNVERIFIED,
           "failure-original-boot-reason");

    make_complete_matrix(&matrix);
    matrix.independent_recovery_channel_verified = 0;
    expect(OSMGAValidateRecoveryMatrix(&matrix, &reason) == 0,
           "recovery-channel-required");
    expect(reason == OSMGA_RECOVERY_MATRIX_RECOVERY_CHANNEL_UNVERIFIED,
           "recovery-channel-reason");

    make_complete_matrix(&matrix);
    expect(OSMGAValidateRecoveryMatrix(&matrix, &reason) == 1,
           "complete-synthetic-matrix-accepted");
    expect(reason == OSMGA_RECOVERY_MATRIX_OK, "complete-matrix-reason");
    expect(OSMGARecoveryMatrixReasonString(reason)[0] == 'o', "reason-string");

    if (failures != 0) {
        return 1;
    }
    printf("OPENSTEP_MGA_RECOVERY_MATRIX_TEST_STATUS=pass\n");
    return 0;
}
