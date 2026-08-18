/*
 * OpenStepMGAMappingReview.h - offline admission policy for R6 range setup.
 *
 * This is not a DriverKit wrapper.  It validates reviewed configuration data
 * before future code can call a DriverKit mapping API.
 */

#ifndef OPENSTEP_MGA_MAPPING_REVIEW_H
#define OPENSTEP_MGA_MAPPING_REVIEW_H

#include "OpenStepMGAModeReview.h"

typedef enum {
    OSMGA_CACHE_POLICY_UNSPECIFIED = 0,
    OSMGA_CACHE_POLICY_UNCACHED,
    OSMGA_CACHE_POLICY_WRITE_THROUGH,
    OSMGA_CACHE_POLICY_COPY_BACK
} OSMGACachePolicy;

typedef struct {
    OSMGAR3ManualModeReview mode_review;
    int sole_owner_snapshot_verified;
    int recovery_path_verified;
    int range_list_verified;
    int cache_policy_verified;
    unsigned int memory_range_count;
    unsigned int framebuffer_range_index;
    unsigned long framebuffer_range_bytes;
    OSMGACachePolicy cache_policy;
} OSMGAR6MappingReview;

typedef enum {
    OSMGA_R6_MAPPING_REVIEW_OK = 0,
    OSMGA_R6_MAPPING_REVIEW_INVALID_ARGUMENT,
    OSMGA_R6_MAPPING_REVIEW_R3_MODE,
    OSMGA_R6_MAPPING_REVIEW_SOLE_OWNER_UNVERIFIED,
    OSMGA_R6_MAPPING_REVIEW_RECOVERY_UNVERIFIED,
    OSMGA_R6_MAPPING_REVIEW_RANGE_LIST_UNVERIFIED,
    OSMGA_R6_MAPPING_REVIEW_CACHE_POLICY_UNVERIFIED,
    OSMGA_R6_MAPPING_REVIEW_RANGE_INDEX_INVALID,
    OSMGA_R6_MAPPING_REVIEW_RANGE_TOO_SMALL,
    OSMGA_R6_MAPPING_REVIEW_CACHE_POLICY_INVALID
} OSMGAR6MappingReviewReason;

/*
 * Validate reviewed configuration only.  No address is accepted or returned,
 * and this function never maps, reads, or writes a device range.
 */
int OSMGAValidateR6MappingReview(const OSMGAR6MappingReview *review,
                                 OSMGAR6MappingReviewReason *reason);

const char *OSMGAR6MappingReviewReasonString(OSMGAR6MappingReviewReason reason);

#endif /* OPENSTEP_MGA_MAPPING_REVIEW_H */
