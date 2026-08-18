/* Pure-C R6 one-mode transaction policy; no target interfaces. */

#include "OpenStepMGAModeTransaction.h"

static int
same_reference(const char *first, const char *second)
{
    if (first == 0 || second == 0) {
        return first == second;
    }
    while (*first != '\0' && *second != '\0') {
        if (*first != *second) {
            return 0;
        }
        first++;
        second++;
    }
    return *first == *second;
}

static int
same_mode_review(const OSMGAR3ManualModeReview *first,
                 const OSMGAR3ManualModeReview *second)
{
    return first->physical_profile.evidence_mask ==
               second->physical_profile.evidence_mask &&
           same_reference(first->physical_profile.board_evidence_reference,
                          second->physical_profile.board_evidence_reference) &&
           same_reference(first->physical_profile.crosscheck_evidence_reference,
                          second->physical_profile.crosscheck_evidence_reference) &&
           same_reference(first->physical_profile.vram_evidence_reference,
                          second->physical_profile.vram_evidence_reference) &&
           same_reference(first->physical_profile.ramdac_evidence_reference,
                          second->physical_profile.ramdac_evidence_reference) &&
           first->physical_profile.vram_type == second->physical_profile.vram_type &&
           first->physical_profile.physical_vram_bytes ==
               second->physical_profile.physical_vram_bytes &&
           first->physical_profile.applicable_ramdac_khz ==
               second->physical_profile.applicable_ramdac_khz &&
           first->configured_vram_bytes == second->configured_vram_bytes &&
           first->evidence_mask == second->evidence_mask &&
           first->mode.width == second->mode.width &&
           first->mode.height == second->mode.height &&
           first->mode.refresh_millihz == second->mode.refresh_millihz &&
           first->timing.mode.width == second->timing.mode.width &&
           first->timing.mode.height == second->timing.mode.height &&
           first->timing.mode.refresh_millihz ==
               second->timing.mode.refresh_millihz &&
           first->timing.pixel_clock_khz == second->timing.pixel_clock_khz &&
           first->timing.horizontal_front_porch ==
               second->timing.horizontal_front_porch &&
           first->timing.horizontal_sync == second->timing.horizontal_sync &&
           first->timing.horizontal_back_porch ==
               second->timing.horizontal_back_porch &&
           first->timing.vertical_front_porch ==
               second->timing.vertical_front_porch &&
           first->timing.vertical_sync == second->timing.vertical_sync &&
           first->timing.vertical_back_porch ==
               second->timing.vertical_back_porch &&
           first->timing.hsync_positive == second->timing.hsync_positive &&
           first->timing.vsync_positive == second->timing.vsync_positive &&
           first->bits_per_pixel == second->bits_per_pixel &&
           first->pitch_bytes == second->pitch_bytes &&
           first->pitch_alignment_bytes == second->pitch_alignment_bytes &&
           first->pixel_clock_khz == second->pixel_clock_khz &&
           first->mapping_bytes == second->mapping_bytes;
}

int
OSMGABeginModeTransaction(const OSMGAR6MappingReview *mapping_review,
                          const OSMGARecoveryMatrix *recovery_matrix,
                          const OSMGAG450PLLReview *pll_review,
                          unsigned long framebuffer_physical_start,
                          const OSMGABoundedPollPolicy *pll_lock_policy,
                          OSMGAModeTransaction *transaction,
                          OSMGAModeTransactionReason *reason)
{
    OSMGAR6MappingReviewReason mapping_reason;
    OSMGARecoveryMatrixReason recovery_reason;
    OSMGAG450CRTCPlanReason crtc_reason;
    OSMGAG450PrimaryCRTCReason primary_crtc_reason;
    OSMGAG450PLLEncodingReason encoding_reason;
    OSMGAG450PLLReason pll_reason;
    OSMGAG450RangePlanReason range_reason;

    if (reason == 0 || transaction == 0) {
        return 0;
    }
    if (mapping_review == 0 || recovery_matrix == 0 || pll_review == 0 ||
        pll_lock_policy == 0) {
        *reason = OSMGA_MODE_TRANSACTION_INVALID_ARGUMENT;
        return 0;
    }
    if (!OSMGAValidateR6MappingReview(mapping_review, &mapping_reason)) {
        *reason = OSMGA_MODE_TRANSACTION_MAPPING_REVIEW;
        return 0;
    }
    if (!OSMGAValidateRecoveryMatrix(recovery_matrix, &recovery_reason)) {
        *reason = OSMGA_MODE_TRANSACTION_RECOVERY_MATRIX;
        return 0;
    }
    if (!same_mode_review(&mapping_review->mode_review,
                          &pll_review->mode_review)) {
        *reason = OSMGA_MODE_TRANSACTION_REVIEW_MISMATCH;
        return 0;
    }
    if (!OSMGABuildG450LegacyRangePlan(mapping_review,
                                       framebuffer_physical_start,
                                       &transaction->range_plan,
                                       &range_reason)) {
        *reason = OSMGA_MODE_TRANSACTION_RANGE_PLAN;
        return 0;
    }
    if (!OSMGABuildG450CRTCPlan(&mapping_review->mode_review,
                                &transaction->crtc_plan, &crtc_reason)) {
        *reason = OSMGA_MODE_TRANSACTION_CRTC_PLAN;
        return 0;
    }
    if (!OSMGABuildG450PrimaryCRTCImage(&mapping_review->mode_review,
                                        &transaction->primary_crtc_image,
                                        &primary_crtc_reason)) {
        *reason = OSMGA_MODE_TRANSACTION_PRIMARY_CRTC_IMAGE;
        return 0;
    }
    if (!OSMGAPlanG450PixelPLL(pll_review, &transaction->pll_plan,
                               &pll_reason)) {
        *reason = OSMGA_MODE_TRANSACTION_PLL_REVIEW;
        return 0;
    }
    if (!OSMGAEncodeG450PLLByteImage(pll_review, &transaction->pll_plan,
                                     &transaction->pll_byte_image,
                                     &encoding_reason)) {
        *reason = OSMGA_MODE_TRANSACTION_PLL_ENCODING;
        return 0;
    }
    if (!OSMGAInitializeBoundedPoll(pll_lock_policy, &transaction->pll_lock)) {
        *reason = OSMGA_MODE_TRANSACTION_POLL_POLICY;
        return 0;
    }
    transaction->rollback_restore_mask = 0U;
    transaction->state = OSMGA_MODE_TRANSACTION_PREFLIGHT_READY;
    *reason = OSMGA_MODE_TRANSACTION_OK;
    return 1;
}

int
OSMGABeginPLLLock(OSMGAModeTransaction *transaction,
                  OSMGAModeTransactionReason *reason)
{
    if (reason == 0 || transaction == 0) {
        return 0;
    }
    if (transaction->state != OSMGA_MODE_TRANSACTION_PREFLIGHT_READY) {
        *reason = OSMGA_MODE_TRANSACTION_INVALID_STATE;
        return 0;
    }
    transaction->state = OSMGA_MODE_TRANSACTION_PLL_LOCK_PENDING;
    *reason = OSMGA_MODE_TRANSACTION_OK;
    return 1;
}

OSMGAModeTransactionReason
OSMGAObservePLLLock(const OSMGABoundedPollPolicy *pll_lock_policy,
                    OSMGAModeTransaction *transaction,
                    unsigned long elapsed_msec, int locked)
{
    OSMGABoundedPollResult result;

    if (pll_lock_policy == 0 || transaction == 0) {
        return OSMGA_MODE_TRANSACTION_INVALID_ARGUMENT;
    }
    if (transaction->state != OSMGA_MODE_TRANSACTION_PLL_LOCK_PENDING) {
        return OSMGA_MODE_TRANSACTION_INVALID_STATE;
    }
    result = OSMGAObserveBoundedPoll(pll_lock_policy, &transaction->pll_lock,
                                    elapsed_msec, locked);
    if (result == OSMGA_BOUNDED_POLL_CONTINUE) {
        return OSMGA_MODE_TRANSACTION_OK;
    }
    if (result == OSMGA_BOUNDED_POLL_READY) {
        transaction->state = OSMGA_MODE_TRANSACTION_PLL_LOCKED;
        return OSMGA_MODE_TRANSACTION_OK;
    }
    transaction->rollback_restore_mask = 0U;
    transaction->state = OSMGA_MODE_TRANSACTION_ROLLBACK_REQUIRED;
    return result == OSMGA_BOUNDED_POLL_TIMEOUT ?
           OSMGA_MODE_TRANSACTION_PLL_TIMEOUT :
           OSMGA_MODE_TRANSACTION_POLL_POLICY;
}

OSMGAModeTransactionReason
OSMGAReportLinearModeEntry(OSMGAModeTransaction *transaction, int succeeded)
{
    if (transaction == 0) {
        return OSMGA_MODE_TRANSACTION_INVALID_ARGUMENT;
    }
    if (transaction->state != OSMGA_MODE_TRANSACTION_PRIMARY_CRTC_VERIFIED) {
        return OSMGA_MODE_TRANSACTION_INVALID_STATE;
    }
    if (!succeeded) {
        transaction->rollback_restore_mask = 0U;
        transaction->state = OSMGA_MODE_TRANSACTION_ROLLBACK_REQUIRED;
        return OSMGA_MODE_TRANSACTION_LINEAR_FAILURE;
    }
    transaction->state = OSMGA_MODE_TRANSACTION_LINEAR_ACTIVE;
    return OSMGA_MODE_TRANSACTION_OK;
}

OSMGAModeTransactionReason
OSMGAReportPrimaryCRTCReadback(
    OSMGAModeTransaction *transaction,
    const OSMGAG450PrimaryCRTCReadback *observed)
{
    OSMGAG450PrimaryCRTCReadbackReason readback_reason;

    if (transaction == 0 || observed == 0) {
        return OSMGA_MODE_TRANSACTION_INVALID_ARGUMENT;
    }
    if (transaction->state != OSMGA_MODE_TRANSACTION_PLL_LOCKED) {
        return OSMGA_MODE_TRANSACTION_INVALID_STATE;
    }
    if (!OSMGAValidateG450PrimaryCRTCReadback(&transaction->primary_crtc_image,
                                              observed, &readback_reason)) {
        transaction->rollback_restore_mask = 0U;
        transaction->state = OSMGA_MODE_TRANSACTION_ROLLBACK_REQUIRED;
        return OSMGA_MODE_TRANSACTION_PRIMARY_CRTC_READBACK;
    }
    transaction->state = OSMGA_MODE_TRANSACTION_PRIMARY_CRTC_VERIFIED;
    return OSMGA_MODE_TRANSACTION_OK;
}

int
OSMGARequireModeRollback(OSMGAModeTransaction *transaction,
                         OSMGAModeTransactionReason *reason)
{
    if (reason == 0 || transaction == 0) {
        return 0;
    }
    if (transaction->state == OSMGA_MODE_TRANSACTION_ROLLED_BACK ||
        transaction->state == OSMGA_MODE_TRANSACTION_IDLE) {
        *reason = OSMGA_MODE_TRANSACTION_INVALID_STATE;
        return 0;
    }
    if (transaction->state != OSMGA_MODE_TRANSACTION_ROLLBACK_REQUIRED) {
        transaction->rollback_restore_mask = 0U;
    }
    transaction->state = OSMGA_MODE_TRANSACTION_ROLLBACK_REQUIRED;
    *reason = OSMGA_MODE_TRANSACTION_OK;
    return 1;
}

OSMGAModeTransactionReason
OSMGAReportModeRollbackStage(OSMGAModeTransaction *transaction,
                             OSMGAModeRollbackStage stage, int restored)
{
    unsigned int stage_mask;

    if (transaction == 0) {
        return OSMGA_MODE_TRANSACTION_INVALID_ARGUMENT;
    }
    stage_mask = (unsigned int)stage;
    if (stage_mask == 0U || (stage_mask & ~OSMGA_ROLLBACK_STAGE_ALL) != 0U ||
        (stage_mask & (stage_mask - 1U)) != 0U) {
        return OSMGA_MODE_TRANSACTION_INVALID_ARGUMENT;
    }
    if (transaction->state != OSMGA_MODE_TRANSACTION_ROLLBACK_REQUIRED) {
        return OSMGA_MODE_TRANSACTION_INVALID_STATE;
    }
    if (!restored) {
        return OSMGA_MODE_TRANSACTION_ROLLBACK_STAGE_FAILURE;
    }
    transaction->rollback_restore_mask |= stage_mask;
    return OSMGA_MODE_TRANSACTION_OK;
}

int
OSMGACompleteModeRollback(OSMGAModeTransaction *transaction,
                          OSMGAModeTransactionReason *reason)
{
    if (reason == 0 || transaction == 0) {
        return 0;
    }
    if (transaction->state != OSMGA_MODE_TRANSACTION_ROLLBACK_REQUIRED) {
        *reason = OSMGA_MODE_TRANSACTION_INVALID_STATE;
        return 0;
    }
    if (transaction->rollback_restore_mask != OSMGA_ROLLBACK_STAGE_ALL) {
        *reason = OSMGA_MODE_TRANSACTION_ROLLBACK_INCOMPLETE;
        return 0;
    }
    transaction->state = OSMGA_MODE_TRANSACTION_ROLLED_BACK;
    *reason = OSMGA_MODE_TRANSACTION_OK;
    return 1;
}

const char *
OSMGAModeTransactionReasonString(OSMGAModeTransactionReason reason)
{
    switch (reason) {
    case OSMGA_MODE_TRANSACTION_OK:
        return "ok";
    case OSMGA_MODE_TRANSACTION_INVALID_ARGUMENT:
        return "invalid-argument";
    case OSMGA_MODE_TRANSACTION_MAPPING_REVIEW:
        return "mapping-review";
    case OSMGA_MODE_TRANSACTION_RECOVERY_MATRIX:
        return "recovery-matrix";
    case OSMGA_MODE_TRANSACTION_PLL_REVIEW:
        return "pll-review";
    case OSMGA_MODE_TRANSACTION_CRTC_PLAN:
        return "crtc-plan";
    case OSMGA_MODE_TRANSACTION_PRIMARY_CRTC_IMAGE:
        return "primary-crtc-image";
    case OSMGA_MODE_TRANSACTION_PRIMARY_CRTC_READBACK:
        return "primary-crtc-readback";
    case OSMGA_MODE_TRANSACTION_PLL_ENCODING:
        return "pll-encoding";
    case OSMGA_MODE_TRANSACTION_RANGE_PLAN:
        return "range-plan";
    case OSMGA_MODE_TRANSACTION_REVIEW_MISMATCH:
        return "review-mismatch";
    case OSMGA_MODE_TRANSACTION_POLL_POLICY:
        return "poll-policy";
    case OSMGA_MODE_TRANSACTION_INVALID_STATE:
        return "invalid-state";
    case OSMGA_MODE_TRANSACTION_PLL_TIMEOUT:
        return "pll-timeout";
    case OSMGA_MODE_TRANSACTION_LINEAR_FAILURE:
        return "linear-failure";
    case OSMGA_MODE_TRANSACTION_ROLLBACK_STAGE_FAILURE:
        return "rollback-stage-failure";
    case OSMGA_MODE_TRANSACTION_ROLLBACK_INCOMPLETE:
        return "rollback-incomplete";
    }
    return "unknown";
}
