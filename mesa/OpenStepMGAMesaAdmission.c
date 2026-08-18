/* No-hardware composition of R3/R6 review and fixed Mesa render budget. */

#include "OpenStepMGAMesaAdmission.h"

int
OSMGABuildMesaBackendRequest(const OSMGAR6MappingReview *mapping_review,
                             const OSMGARenderBudgetRequest *budget_request,
                             int presentation_contract_verified,
                             int fence_review_verified,
                             int software_fallback_available,
                             OSMGAMesaBackendRequest *backend_request,
                             OSMGARenderBudgetResult *budget_result,
                             OSMGAMesaAdmissionReason *reason)
{
    OSMGAR6MappingReviewReason mapping_reason;
    OSMGAR3ModeReviewReason mode_reason;
    OSMGARenderBudgetReason render_reason;
    unsigned long scanout_bytes;

    if (reason == 0 || backend_request == 0 || budget_result == 0) {
        return 0;
    }
    if (mapping_review == 0 || budget_request == 0) {
        *reason = OSMGA_MESA_ADMISSION_INVALID_ARGUMENT;
        return 0;
    }
    if (!OSMGAValidateR6MappingReview(mapping_review, &mapping_reason)) {
        *reason = OSMGA_MESA_ADMISSION_R6_MAPPING_REVIEW;
        return 0;
    }
    if (!OSMGAValidateR3ManualModeReview(&mapping_review->mode_review,
                                         &scanout_bytes, &mode_reason)) {
        *reason = OSMGA_MESA_ADMISSION_R3_MODE_REVIEW;
        return 0;
    }
    if (budget_request->render_width != OSMGA_MESA_RENDER_WIDTH ||
        budget_request->render_height != OSMGA_MESA_RENDER_HEIGHT ||
        budget_request->color_bits_per_pixel != OSMGA_MESA_COLOR_BITS ||
        budget_request->depth_bits_per_pixel != OSMGA_MESA_DEPTH_BITS ||
        budget_request->color_buffer_count != 2U) {
        *reason = OSMGA_MESA_ADMISSION_FIXED_TARGET_REQUIRED;
        return 0;
    }
    if (budget_request->available_bytes !=
        mapping_review->mode_review.physical_profile.physical_vram_bytes) {
        *reason = OSMGA_MESA_ADMISSION_PROFILE_BUDGET_MISMATCH;
        return 0;
    }
    if (budget_request->scanout_bytes != scanout_bytes) {
        *reason = OSMGA_MESA_ADMISSION_SCANOUT_BUDGET_MISMATCH;
        return 0;
    }
    if (budget_request->pitch_alignment_bytes !=
        mapping_review->mode_review.pitch_alignment_bytes) {
        *reason = OSMGA_MESA_ADMISSION_ALIGNMENT_MISMATCH;
        return 0;
    }
    if (!OSMGAValidateRenderBudget(budget_request, budget_result,
                                   &render_reason)) {
        *reason = OSMGA_MESA_ADMISSION_RENDER_BUDGET;
        return 0;
    }

    backend_request->render_width = OSMGA_MESA_RENDER_WIDTH;
    backend_request->render_height = OSMGA_MESA_RENDER_HEIGHT;
    backend_request->color_bits_per_pixel = OSMGA_MESA_COLOR_BITS;
    backend_request->depth_bits_per_pixel = OSMGA_MESA_DEPTH_BITS;
    backend_request->presentation_contract_verified =
        presentation_contract_verified != 0;
    backend_request->r1_sole_owner_verified =
        mapping_review->sole_owner_snapshot_verified != 0;
    backend_request->r2_physical_profile_verified = 1;
    backend_request->r3_mode_record_verified = 1;
    backend_request->r4_recovery_verified =
        mapping_review->recovery_path_verified != 0;
    backend_request->mapping_review_verified = 1;
    backend_request->render_budget_verified = 1;
    backend_request->fence_review_verified = fence_review_verified != 0;
    backend_request->software_fallback_available =
        software_fallback_available != 0;
    *reason = OSMGA_MESA_ADMISSION_OK;
    return 1;
}

const char *
OSMGAMesaAdmissionReasonString(OSMGAMesaAdmissionReason reason)
{
    switch (reason) {
    case OSMGA_MESA_ADMISSION_OK:
        return "ok";
    case OSMGA_MESA_ADMISSION_INVALID_ARGUMENT:
        return "invalid-argument";
    case OSMGA_MESA_ADMISSION_R6_MAPPING_REVIEW:
        return "r6-mapping-review";
    case OSMGA_MESA_ADMISSION_R3_MODE_REVIEW:
        return "r3-mode-review";
    case OSMGA_MESA_ADMISSION_FIXED_TARGET_REQUIRED:
        return "fixed-target-required";
    case OSMGA_MESA_ADMISSION_PROFILE_BUDGET_MISMATCH:
        return "profile-budget-mismatch";
    case OSMGA_MESA_ADMISSION_SCANOUT_BUDGET_MISMATCH:
        return "scanout-budget-mismatch";
    case OSMGA_MESA_ADMISSION_ALIGNMENT_MISMATCH:
        return "alignment-mismatch";
    case OSMGA_MESA_ADMISSION_RENDER_BUDGET:
        return "render-budget";
    }
    return "unknown";
}
