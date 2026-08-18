/* Pure-C R6 configuration admission policy; no target interfaces. */

#include "OpenStepMGAMappingReview.h"

int
OSMGAValidateR6MappingReview(const OSMGAR6MappingReview *review,
                             OSMGAR6MappingReviewReason *reason)
{
    OSMGAR3ModeReviewReason mode_reason;
    unsigned long required_bytes;

    if (reason == 0) {
        return 0;
    }
    if (review == 0) {
        *reason = OSMGA_R6_MAPPING_REVIEW_INVALID_ARGUMENT;
        return 0;
    }
    required_bytes = 0;
    if (!OSMGAValidateR3ManualModeReview(&review->mode_review,
                                         &required_bytes, &mode_reason)) {
        *reason = OSMGA_R6_MAPPING_REVIEW_R3_MODE;
        return 0;
    }
    if (!review->sole_owner_snapshot_verified) {
        *reason = OSMGA_R6_MAPPING_REVIEW_SOLE_OWNER_UNVERIFIED;
        return 0;
    }
    if (!review->recovery_path_verified) {
        *reason = OSMGA_R6_MAPPING_REVIEW_RECOVERY_UNVERIFIED;
        return 0;
    }
    if (!review->range_list_verified) {
        *reason = OSMGA_R6_MAPPING_REVIEW_RANGE_LIST_UNVERIFIED;
        return 0;
    }
    if (!review->cache_policy_verified) {
        *reason = OSMGA_R6_MAPPING_REVIEW_CACHE_POLICY_UNVERIFIED;
        return 0;
    }
    if (review->memory_range_count == 0 ||
        review->framebuffer_range_index >= review->memory_range_count) {
        *reason = OSMGA_R6_MAPPING_REVIEW_RANGE_INDEX_INVALID;
        return 0;
    }
    if (review->framebuffer_range_bytes < required_bytes) {
        *reason = OSMGA_R6_MAPPING_REVIEW_RANGE_TOO_SMALL;
        return 0;
    }
    if (review->cache_policy != OSMGA_CACHE_POLICY_UNCACHED &&
        review->cache_policy != OSMGA_CACHE_POLICY_WRITE_THROUGH &&
        review->cache_policy != OSMGA_CACHE_POLICY_COPY_BACK) {
        *reason = OSMGA_R6_MAPPING_REVIEW_CACHE_POLICY_INVALID;
        return 0;
    }
    *reason = OSMGA_R6_MAPPING_REVIEW_OK;
    return 1;
}

const char *
OSMGAR6MappingReviewReasonString(OSMGAR6MappingReviewReason reason)
{
    switch (reason) {
    case OSMGA_R6_MAPPING_REVIEW_OK:
        return "ok";
    case OSMGA_R6_MAPPING_REVIEW_INVALID_ARGUMENT:
        return "invalid-argument";
    case OSMGA_R6_MAPPING_REVIEW_R3_MODE:
        return "r3-mode";
    case OSMGA_R6_MAPPING_REVIEW_SOLE_OWNER_UNVERIFIED:
        return "sole-owner-unverified";
    case OSMGA_R6_MAPPING_REVIEW_RECOVERY_UNVERIFIED:
        return "recovery-unverified";
    case OSMGA_R6_MAPPING_REVIEW_RANGE_LIST_UNVERIFIED:
        return "range-list-unverified";
    case OSMGA_R6_MAPPING_REVIEW_CACHE_POLICY_UNVERIFIED:
        return "cache-policy-unverified";
    case OSMGA_R6_MAPPING_REVIEW_RANGE_INDEX_INVALID:
        return "range-index-invalid";
    case OSMGA_R6_MAPPING_REVIEW_RANGE_TOO_SMALL:
        return "range-too-small";
    case OSMGA_R6_MAPPING_REVIEW_CACHE_POLICY_INVALID:
        return "cache-policy-invalid";
    }
    return "unknown";
}
