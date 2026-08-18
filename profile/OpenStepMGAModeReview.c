/* Pure-C R3 manual-mode review policy; no target interfaces. */

#include "OpenStepMGAModeReview.h"

int
OSMGAValidateR3ManualModeReview(const OSMGAR3ManualModeReview *review,
                                unsigned long *required_bytes,
                                OSMGAR3ModeReviewReason *reason)
{
    OSMGAR2ProfileReason profile_reason;
    OSMGAModeMemoryReason memory_reason;
    OSMGATimingReviewReason timing_reason;
    unsigned long required;

    if (reason == 0 || required_bytes == 0) {
        return 0;
    }
    if (review == 0) {
        *reason = OSMGA_R3_MODE_REVIEW_INVALID_ARGUMENT;
        return 0;
    }
    if (!OSMGAValidateR2PhysicalProfile(&review->physical_profile,
                                        &profile_reason)) {
        *reason = OSMGA_R3_MODE_REVIEW_R2_PROFILE;
        return 0;
    }
    if (review->configured_vram_bytes !=
        review->physical_profile.physical_vram_bytes) {
        *reason = OSMGA_R3_MODE_REVIEW_MANUAL_MEMORY_MISMATCH;
        return 0;
    }
    if ((review->evidence_mask & OSMGA_R3_EVIDENCE_MODE_SOURCE) == 0) {
        *reason = OSMGA_R3_MODE_REVIEW_MODE_SOURCE_UNVERIFIED;
        return 0;
    }
    if ((review->evidence_mask & OSMGA_R3_EVIDENCE_TIMING_SOURCE) == 0) {
        *reason = OSMGA_R3_MODE_REVIEW_TIMING_SOURCE_UNVERIFIED;
        return 0;
    }
    if ((review->evidence_mask & OSMGA_R3_EVIDENCE_PITCH_POLICY) == 0) {
        *reason = OSMGA_R3_MODE_REVIEW_PITCH_POLICY_UNVERIFIED;
        return 0;
    }
    if ((review->evidence_mask & OSMGA_R3_EVIDENCE_MAPPING_BOUND) == 0) {
        *reason = OSMGA_R3_MODE_REVIEW_MAPPING_BOUND_UNVERIFIED;
        return 0;
    }
    if (review->mode.width == 0 || review->mode.height == 0 ||
        review->mode.refresh_millihz == 0) {
        *reason = OSMGA_R3_MODE_REVIEW_INVALID_MODE;
        return 0;
    }
    if (review->pixel_clock_khz == 0) {
        *reason = OSMGA_R3_MODE_REVIEW_INVALID_PIXEL_CLOCK;
        return 0;
    }
    if (!OSMGAValidateTimingReview(&review->timing, 0, &timing_reason)) {
        *reason = OSMGA_R3_MODE_REVIEW_TIMING_RECORD_INVALID;
        return 0;
    }
    if (review->timing.mode.width != review->mode.width ||
        review->timing.mode.height != review->mode.height ||
        review->timing.mode.refresh_millihz != review->mode.refresh_millihz ||
        review->timing.pixel_clock_khz != review->pixel_clock_khz) {
        *reason = OSMGA_R3_MODE_REVIEW_TIMING_RECORD_MISMATCH;
        return 0;
    }
    if (review->pixel_clock_khz > review->physical_profile.applicable_ramdac_khz) {
        *reason = OSMGA_R3_MODE_REVIEW_PIXEL_CLOCK_EXCEEDS_LIMIT;
        return 0;
    }
    if (review->pitch_alignment_bytes == 0) {
        *reason = OSMGA_R3_MODE_REVIEW_INVALID_ALIGNMENT;
        return 0;
    }
    if (review->pitch_bytes % review->pitch_alignment_bytes != 0) {
        *reason = OSMGA_R3_MODE_REVIEW_MISALIGNED_PITCH;
        return 0;
    }
    required = 0;
    if (!OSMGAModeFitsLinearMemory(&review->mode, review->bits_per_pixel,
                                   review->pitch_bytes,
                                   review->physical_profile.physical_vram_bytes,
                                   &required, &memory_reason)) {
        *reason = OSMGA_R3_MODE_REVIEW_FRAMEBUFFER_FOOTPRINT;
        return 0;
    }
    if (review->mapping_bytes < required) {
        *reason = OSMGA_R3_MODE_REVIEW_MAPPING_TOO_SMALL;
        return 0;
    }
    if (review->mapping_bytes > review->physical_profile.physical_vram_bytes) {
        *reason = OSMGA_R3_MODE_REVIEW_MAPPING_EXCEEDS_VRAM;
        return 0;
    }
    *required_bytes = required;
    *reason = OSMGA_R3_MODE_REVIEW_OK;
    return 1;
}

const char *
OSMGAR3ModeReviewReasonString(OSMGAR3ModeReviewReason reason)
{
    switch (reason) {
    case OSMGA_R3_MODE_REVIEW_OK:
        return "ok";
    case OSMGA_R3_MODE_REVIEW_INVALID_ARGUMENT:
        return "invalid-argument";
    case OSMGA_R3_MODE_REVIEW_R2_PROFILE:
        return "r2-profile";
    case OSMGA_R3_MODE_REVIEW_MANUAL_MEMORY_MISMATCH:
        return "manual-memory-mismatch";
    case OSMGA_R3_MODE_REVIEW_MODE_SOURCE_UNVERIFIED:
        return "mode-source-unverified";
    case OSMGA_R3_MODE_REVIEW_TIMING_SOURCE_UNVERIFIED:
        return "timing-source-unverified";
    case OSMGA_R3_MODE_REVIEW_TIMING_RECORD_INVALID:
        return "timing-record-invalid";
    case OSMGA_R3_MODE_REVIEW_TIMING_RECORD_MISMATCH:
        return "timing-record-mismatch";
    case OSMGA_R3_MODE_REVIEW_PITCH_POLICY_UNVERIFIED:
        return "pitch-policy-unverified";
    case OSMGA_R3_MODE_REVIEW_MAPPING_BOUND_UNVERIFIED:
        return "mapping-bound-unverified";
    case OSMGA_R3_MODE_REVIEW_INVALID_MODE:
        return "invalid-mode";
    case OSMGA_R3_MODE_REVIEW_INVALID_PIXEL_CLOCK:
        return "invalid-pixel-clock";
    case OSMGA_R3_MODE_REVIEW_PIXEL_CLOCK_EXCEEDS_LIMIT:
        return "pixel-clock-exceeds-limit";
    case OSMGA_R3_MODE_REVIEW_INVALID_ALIGNMENT:
        return "invalid-alignment";
    case OSMGA_R3_MODE_REVIEW_MISALIGNED_PITCH:
        return "misaligned-pitch";
    case OSMGA_R3_MODE_REVIEW_FRAMEBUFFER_FOOTPRINT:
        return "framebuffer-footprint";
    case OSMGA_R3_MODE_REVIEW_MAPPING_TOO_SMALL:
        return "mapping-too-small";
    case OSMGA_R3_MODE_REVIEW_MAPPING_EXCEEDS_VRAM:
        return "mapping-exceeds-vram";
    }
    return "unknown";
}
