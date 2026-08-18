#include <stdio.h>

#include "OpenStepMGAModeTransaction.h"

static int failures;

static void
expect(int condition, const char *name)
{
    if (!condition) {
        printf("OPENSTEP_MGA_MODE_TRANSACTION_TEST=fail:%s\n", name);
        failures++;
    }
}

static void
make_mode_review(OSMGAR3ManualModeReview *review)
{
    review->physical_profile.evidence_mask =
        OSMGA_R2_EVIDENCE_BOARD_ID |
        OSMGA_R2_EVIDENCE_INDEPENDENT_CROSSCHECK |
        OSMGA_R2_EVIDENCE_VRAM_TYPE |
        OSMGA_R2_EVIDENCE_VRAM_SIZE |
        OSMGA_R2_EVIDENCE_RAMDAC_LIMIT;
    review->physical_profile.board_evidence_reference = "B2";
    review->physical_profile.crosscheck_evidence_reference = "B6";
    review->physical_profile.vram_evidence_reference = "B4+B5";
    review->physical_profile.ramdac_evidence_reference = "B5";
    review->physical_profile.vram_type = OSMGA_VRAM_TYPE_DDR_SDRAM;
    review->physical_profile.physical_vram_bytes = 16UL * 1024UL * 1024UL;
    review->physical_profile.applicable_ramdac_khz = 300000UL;
    review->configured_vram_bytes = 16UL * 1024UL * 1024UL;
    review->evidence_mask = OSMGA_R3_EVIDENCE_MODE_SOURCE |
                            OSMGA_R3_EVIDENCE_TIMING_SOURCE |
                            OSMGA_R3_EVIDENCE_PITCH_POLICY |
                            OSMGA_R3_EVIDENCE_MAPPING_BOUND;
    review->mode.width = 1600;
    review->mode.height = 1200;
    review->mode.refresh_millihz = 60000UL;
    review->timing.mode = review->mode;
    review->timing.pixel_clock_khz = 162000UL;
    review->timing.horizontal_front_porch = 64UL;
    review->timing.horizontal_sync = 192UL;
    review->timing.horizontal_back_porch = 304UL;
    review->timing.vertical_front_porch = 1UL;
    review->timing.vertical_sync = 3UL;
    review->timing.vertical_back_porch = 46UL;
    review->timing.hsync_positive = 1;
    review->timing.vsync_positive = 1;
    review->bits_per_pixel = 32;
    review->pitch_bytes = 6400UL;
    review->pitch_alignment_bytes = 8UL;
    review->pixel_clock_khz = 162000UL;
    review->mapping_bytes = 16UL * 1024UL * 1024UL;
}

static void
make_complete_inputs(OSMGAR6MappingReview *mapping,
                     OSMGAG450PLLReview *pll,
                     OSMGABoundedPollPolicy *poll,
                     OSMGARecoveryMatrix *recovery)
{
    make_mode_review(&mapping->mode_review);
    mapping->sole_owner_snapshot_verified = 1;
    mapping->recovery_path_verified = 1;
    mapping->range_list_verified = 1;
    mapping->cache_policy_verified = 1;
    mapping->memory_range_count = 3;
    mapping->framebuffer_range_index = 0;
    mapping->framebuffer_range_bytes = 16UL * 1024UL * 1024UL;
    mapping->cache_policy = OSMGA_CACHE_POLICY_WRITE_THROUGH;
    pll->mode_review = mapping->mode_review;
    pll->pll_source_verified = 1;
    pll->head_selection_verified = 1;
    pll->head = OSMGA_G450_HEAD_PRIMARY;
    poll->timeout_msec = 10UL;
    poll->required_consecutive_ready = 2;
    recovery->original.original_candidate_count = 1U;
    recovery->original.replacement_candidate_count = 0U;
    recovery->recovery.original_candidate_count = 0U;
    recovery->recovery.replacement_candidate_count = 1U;
    recovery->failure.original_candidate_count = 1U;
    recovery->failure.replacement_candidate_count = 0U;
    recovery->original.bundle_verified = 1;
    recovery->original.instance_table_verified = 1;
    recovery->original.rollback_instructions_verified = 1;
    recovery->recovery.bundle_verified = 1;
    recovery->recovery.instance_table_verified = 1;
    recovery->recovery.rollback_instructions_verified = 1;
    recovery->failure.bundle_verified = 1;
    recovery->failure.instance_table_verified = 1;
    recovery->failure.rollback_instructions_verified = 1;
    recovery->atomic_install_verified = 1;
    recovery->installer_rollback_verified = 1;
    recovery->failure_original_boot_verified = 1;
    recovery->independent_recovery_channel_verified = 1;
}

static void
make_primary_readback(const OSMGAModeTransaction *transaction,
                      OSMGAG450PrimaryCRTCReadback *observed)
{
    unsigned int index;

    for (index = 0U; index < OSMGA_G450_PRIMARY_CRTC_REGISTER_COUNT; index++) {
        observed->crtc[index] = transaction->primary_crtc_image.crtc[index];
    }
    for (index = 0U; index < OSMGA_G450_PRIMARY_EXT_REGISTER_COUNT; index++) {
        observed->extended[index] = transaction->primary_crtc_image.extended[index];
    }
    observed->misc_output = transaction->primary_crtc_image.misc_output_or;
}

static int
report_all_rollback_stages(OSMGAModeTransaction *transaction)
{
    return OSMGAReportModeRollbackStage(
               transaction, OSMGA_ROLLBACK_STAGE_DISPLAY_STATE, 1) ==
               OSMGA_MODE_TRANSACTION_OK &&
           OSMGAReportModeRollbackStage(
               transaction, OSMGA_ROLLBACK_STAGE_PLL_STATE, 1) ==
               OSMGA_MODE_TRANSACTION_OK &&
           OSMGAReportModeRollbackStage(
               transaction, OSMGA_ROLLBACK_STAGE_VGA_SAFE_STATE, 1) ==
               OSMGA_MODE_TRANSACTION_OK &&
           OSMGAReportModeRollbackStage(
               transaction, OSMGA_ROLLBACK_STAGE_SUPERCLASS_REVERT, 1) ==
               OSMGA_MODE_TRANSACTION_OK;
}

int
main(void)
{
    OSMGAR6MappingReview mapping;
    OSMGAG450PLLReview pll;
    OSMGABoundedPollPolicy poll;
    OSMGARecoveryMatrix recovery;
    OSMGAModeTransaction transaction;
    OSMGAModeTransactionReason reason;
    OSMGAG450PrimaryCRTCReadback observed;

    make_complete_inputs(&mapping, &pll, &poll, &recovery);
    expect(OSMGABeginModeTransaction(&mapping, &recovery, &pll, 0xf0000000UL, &poll, &transaction,
                                     &reason) == 1, "preflight");
    expect(reason == OSMGA_MODE_TRANSACTION_OK, "preflight-reason");
    expect(transaction.state == OSMGA_MODE_TRANSACTION_PREFLIGHT_READY,
           "preflight-state");
    expect(transaction.crtc_plan.horizontal_display == 1600UL,
           "crtc-horizontal-display");
    expect(transaction.crtc_plan.horizontal_total == 2160UL,
           "crtc-horizontal-total");
    expect(transaction.crtc_plan.vertical_total == 1250UL,
           "crtc-vertical-total");
    expect(transaction.crtc_plan.pitch_bytes == 6400UL,
           "crtc-pitch");
    expect(transaction.crtc_plan.scanout_bytes == 7680000UL,
           "crtc-scanout");
    expect(transaction.primary_crtc_image.crtc[0] == 0x09U &&
               transaction.primary_crtc_image.crtc[1] == 0xc7U &&
               transaction.primary_crtc_image.crtc[22] == 0xe1U,
           "primary-crtc-image");
    expect(transaction.pll_byte_image.target ==
               OSMGA_G450_PLL_DAC_PRIMARY_PIXEL_C,
           "pll-byte-image-target");
    expect(transaction.pll_byte_image.m == 0U &&
               transaction.pll_byte_image.n == 4U &&
               transaction.pll_byte_image.p == 0U,
           "pll-byte-image-162mhz");
    expect(transaction.range_plan.framebuffer_index == 0U &&
               transaction.range_plan.ranges[0].length_bytes ==
                   16UL * 1024UL * 1024UL,
           "range-plan-framebuffer");
    expect(OSMGAReportLinearModeEntry(&transaction, 1) ==
           OSMGA_MODE_TRANSACTION_INVALID_STATE, "linear-before-lock-rejected");
    expect(OSMGABeginPLLLock(&transaction, &reason) == 1, "begin-lock");
    expect(OSMGAObservePLLLock(&poll, &transaction, 1UL, 1) ==
           OSMGA_MODE_TRANSACTION_OK, "first-lock-sample");
    expect(transaction.state == OSMGA_MODE_TRANSACTION_PLL_LOCK_PENDING,
           "lock-pending-state");
    expect(OSMGAObservePLLLock(&poll, &transaction, 2UL, 1) ==
           OSMGA_MODE_TRANSACTION_OK, "stable-lock");
    expect(transaction.state == OSMGA_MODE_TRANSACTION_PLL_LOCKED,
           "locked-state");
    expect(OSMGAReportLinearModeEntry(&transaction, 1) ==
           OSMGA_MODE_TRANSACTION_INVALID_STATE,
           "linear-before-crtc-readback-rejected");
    make_primary_readback(&transaction, &observed);
    expect(OSMGAReportPrimaryCRTCReadback(&transaction, &observed) ==
           OSMGA_MODE_TRANSACTION_OK, "primary-crtc-readback");
    expect(transaction.state == OSMGA_MODE_TRANSACTION_PRIMARY_CRTC_VERIFIED,
           "primary-crtc-verified-state");
    expect(OSMGAReportLinearModeEntry(&transaction, 1) ==
           OSMGA_MODE_TRANSACTION_OK, "linear-active");
    expect(transaction.state == OSMGA_MODE_TRANSACTION_LINEAR_ACTIVE,
           "linear-active-state");
    expect(OSMGARequireModeRollback(&transaction, &reason) == 1,
           "rollback-required");
    expect(OSMGACompleteModeRollback(&transaction, &reason) == 0,
           "incomplete-rollback-rejected");
    expect(reason == OSMGA_MODE_TRANSACTION_ROLLBACK_INCOMPLETE,
           "incomplete-rollback-reason");
    expect(OSMGAReportModeRollbackStage(
               &transaction, OSMGA_ROLLBACK_STAGE_PLL_STATE, 0) ==
               OSMGA_MODE_TRANSACTION_ROLLBACK_STAGE_FAILURE,
           "failed-rollback-stage-rejected");
    expect(report_all_rollback_stages(&transaction) == 1,
           "all-rollback-stages-reported");
    expect(OSMGACompleteModeRollback(&transaction, &reason) == 1,
           "rollback-complete");
    expect(transaction.state == OSMGA_MODE_TRANSACTION_ROLLED_BACK,
           "rolled-back-state");

    make_complete_inputs(&mapping, &pll, &poll, &recovery);
    recovery.recovery.original_candidate_count = 1U;
    expect(OSMGABeginModeTransaction(&mapping, &recovery, &pll, 0xf0000000UL,
                                     &poll, &transaction, &reason) == 0,
           "recovery-dual-owner-rejected");
    expect(reason == OSMGA_MODE_TRANSACTION_RECOVERY_MATRIX,
           "recovery-matrix-reason");

    make_complete_inputs(&mapping, &pll, &poll, &recovery);
    pll.mode_review.pixel_clock_khz = 161000UL;
    expect(OSMGABeginModeTransaction(&mapping, &recovery, &pll, 0xf0000000UL, &poll, &transaction,
                                     &reason) == 0, "mismatched-review-rejected");
    expect(reason == OSMGA_MODE_TRANSACTION_REVIEW_MISMATCH,
           "mismatched-review-reason");

    make_complete_inputs(&mapping, &pll, &poll, &recovery);
    pll.mode_review.physical_profile.vram_evidence_reference = "different-B4+B5";
    expect(OSMGABeginModeTransaction(&mapping, &recovery, &pll, 0xf0000000UL, &poll, &transaction,
                                     &reason) == 0,
           "mismatched-evidence-reference-rejected");
    expect(reason == OSMGA_MODE_TRANSACTION_REVIEW_MISMATCH,
           "mismatched-evidence-reference-reason");

    make_complete_inputs(&mapping, &pll, &poll, &recovery);
    pll.mode_review.configured_vram_bytes = 8UL * 1024UL * 1024UL;
    expect(OSMGABeginModeTransaction(&mapping, &recovery, &pll, 0xf0000000UL, &poll, &transaction,
                                     &reason) == 0,
           "mismatched-manual-memory-rejected");
    expect(reason == OSMGA_MODE_TRANSACTION_REVIEW_MISMATCH,
           "mismatched-manual-memory-reason");

    make_complete_inputs(&mapping, &pll, &poll, &recovery);
    pll.mode_review.timing.horizontal_back_porch = 303UL;
    expect(OSMGABeginModeTransaction(&mapping, &recovery, &pll, 0xf0000000UL, &poll, &transaction,
                                     &reason) == 0,
           "mismatched-timing-shape-rejected");
    expect(reason == OSMGA_MODE_TRANSACTION_REVIEW_MISMATCH,
           "mismatched-timing-shape-reason");

    make_complete_inputs(&mapping, &pll, &poll, &recovery);
    mapping.mode_review.bits_per_pixel = 16;
    pll.mode_review.bits_per_pixel = 16;
    expect(OSMGABeginModeTransaction(&mapping, &recovery, &pll, 0xf0000000UL, &poll, &transaction,
                                     &reason) == 0,
           "unsupported-crtc-format-rejected");
    expect(reason == OSMGA_MODE_TRANSACTION_CRTC_PLAN,
           "unsupported-crtc-format-reason");

    make_complete_inputs(&mapping, &pll, &poll, &recovery);
    expect(OSMGABeginModeTransaction(&mapping, &recovery, &pll, 0xf0000001UL, &poll,
                                     &transaction, &reason) == 0,
           "unaligned-range-base-rejected");
    expect(reason == OSMGA_MODE_TRANSACTION_RANGE_PLAN,
           "unaligned-range-base-reason");

    make_complete_inputs(&mapping, &pll, &poll, &recovery);
    expect(OSMGABeginModeTransaction(&mapping, &recovery, &pll, 0xf0000000UL, &poll,
                                     &transaction, &reason) == 1,
           "readback-failure-preflight");
    expect(OSMGABeginPLLLock(&transaction, &reason) == 1,
           "readback-failure-begin-lock");
    expect(OSMGAObservePLLLock(&poll, &transaction, 1UL, 1) ==
           OSMGA_MODE_TRANSACTION_OK, "readback-failure-first-lock");
    expect(OSMGAObservePLLLock(&poll, &transaction, 2UL, 1) ==
           OSMGA_MODE_TRANSACTION_OK, "readback-failure-stable-lock");
    make_primary_readback(&transaction, &observed);
    observed.extended[2] = 0U;
    expect(OSMGAReportPrimaryCRTCReadback(&transaction, &observed) ==
           OSMGA_MODE_TRANSACTION_PRIMARY_CRTC_READBACK,
           "readback-mismatch-rejected");
    expect(transaction.state == OSMGA_MODE_TRANSACTION_ROLLBACK_REQUIRED,
           "readback-mismatch-requires-rollback");
    expect(report_all_rollback_stages(&transaction) == 1,
           "readback-mismatch-rollback-stages");
    expect(OSMGACompleteModeRollback(&transaction, &reason) == 1,
           "readback-mismatch-rollback-complete");

    make_complete_inputs(&mapping, &pll, &poll, &recovery);
    expect(OSMGABeginModeTransaction(&mapping, &recovery, &pll, 0xf0000000UL, &poll, &transaction,
                                     &reason) == 1, "timeout-preflight");
    expect(OSMGABeginPLLLock(&transaction, &reason) == 1, "timeout-begin-lock");
    expect(OSMGAObservePLLLock(&poll, &transaction, 10UL, 1) ==
           OSMGA_MODE_TRANSACTION_PLL_TIMEOUT, "lock-timeout");
    expect(transaction.state == OSMGA_MODE_TRANSACTION_ROLLBACK_REQUIRED,
           "timeout-requires-rollback");
    expect(report_all_rollback_stages(&transaction) == 1,
           "timeout-rollback-stages");
    expect(OSMGACompleteModeRollback(&transaction, &reason) == 1,
           "timeout-rollback-complete");

    expect(OSMGAModeTransactionReasonString(OSMGA_MODE_TRANSACTION_OK)[0] == 'o',
           "reason-string");
    expect(OSMGAModeTransactionReasonString(
               OSMGA_MODE_TRANSACTION_CRTC_PLAN)[0] == 'c',
           "crtc-reason-string");
    expect(OSMGAModeTransactionReasonString(
               OSMGA_MODE_TRANSACTION_PRIMARY_CRTC_IMAGE)[0] == 'p',
           "primary-crtc-image-reason-string");
    expect(OSMGAModeTransactionReasonString(
               OSMGA_MODE_TRANSACTION_PRIMARY_CRTC_READBACK)[0] == 'p',
           "primary-crtc-readback-reason-string");
    expect(OSMGAModeTransactionReasonString(
               OSMGA_MODE_TRANSACTION_ROLLBACK_INCOMPLETE)[0] == 'r',
           "rollback-incomplete-reason-string");
    expect(OSMGAModeTransactionReasonString(
               OSMGA_MODE_TRANSACTION_PLL_ENCODING)[0] == 'p',
           "pll-encoding-reason-string");
    expect(OSMGAModeTransactionReasonString(
               OSMGA_MODE_TRANSACTION_RANGE_PLAN)[0] == 'r',
           "range-plan-reason-string");
    if (failures != 0) {
        return 1;
    }
    printf("OPENSTEP_MGA_MODE_TRANSACTION_TEST_STATUS=pass\n");
    return 0;
}
