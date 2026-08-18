/* Pure-C G450 pixel-clock candidate arithmetic; no target interfaces. */

#include "OpenStepMGAG450PLL.h"

#define OSMGA_G450_PLL_REFERENCE_KHZ 27000UL
#define OSMGA_G450_PLL_VCO_MIN_KHZ 256000UL
#define OSMGA_G450_PLL_VCO_MAX_KHZ 1300000UL

static unsigned long
absolute_difference(unsigned long first, unsigned long second)
{
    return first >= second ? first - second : second - first;
}

static unsigned long
post_divider_for_index(unsigned int index)
{
    return 2UL << index;
}

int
OSMGAPlanG450PixelPLL(const OSMGAG450PLLReview *review,
                      OSMGAG450PLLPlan *plan,
                      OSMGAG450PLLReason *reason)
{
    OSMGAR3ModeReviewReason mode_reason;
    unsigned long unused_required_bytes;
    unsigned long requested;
    unsigned long best_error;
    unsigned long vco;
    unsigned long numerator;
    unsigned long achieved_vco;
    unsigned long achieved;
    unsigned long error;
    unsigned long divider;
    unsigned long rounded;
    unsigned int post_index;
    unsigned int feedback;
    unsigned int reference;
    int found;

    if (reason == 0 || plan == 0) {
        return 0;
    }
    if (review == 0) {
        *reason = OSMGA_G450_PLL_INVALID_ARGUMENT;
        return 0;
    }
    unused_required_bytes = 0;
    if (!OSMGAValidateR3ManualModeReview(&review->mode_review,
                                         &unused_required_bytes,
                                         &mode_reason)) {
        *reason = OSMGA_G450_PLL_R3_MODE;
        return 0;
    }
    if (!review->pll_source_verified) {
        *reason = OSMGA_G450_PLL_SOURCE_UNVERIFIED;
        return 0;
    }
    if (!review->head_selection_verified) {
        *reason = OSMGA_G450_PLL_HEAD_UNVERIFIED;
        return 0;
    }
    if (review->head != OSMGA_G450_HEAD_PRIMARY &&
        review->head != OSMGA_G450_HEAD_SECONDARY) {
        *reason = OSMGA_G450_PLL_HEAD_INVALID;
        return 0;
    }

    requested = review->mode_review.pixel_clock_khz;
    found = 0;
    best_error = 0;
    for (post_index = 0; post_index <= 3; post_index++) {
        divider = post_divider_for_index(post_index);
        if (requested > 4294967295UL / divider) {
            continue;
        }
        vco = requested * divider;
        if (vco < OSMGA_G450_PLL_VCO_MIN_KHZ ||
            vco > OSMGA_G450_PLL_VCO_MAX_KHZ) {
            continue;
        }
        for (reference = 1; reference <= 10; reference++) {
            numerator = vco * (unsigned long)reference;
            rounded = (numerator + OSMGA_G450_PLL_REFERENCE_KHZ) /
                      (OSMGA_G450_PLL_REFERENCE_KHZ * 2UL);
            if (rounded < 2UL || rounded > 257UL) {
                continue;
            }
            feedback = (unsigned int)(rounded - 2UL);
            achieved_vco = (OSMGA_G450_PLL_REFERENCE_KHZ *
                            (2UL * ((unsigned long)feedback + 2UL))) /
                           (unsigned long)reference;
            achieved = achieved_vco / divider;
            error = absolute_difference(achieved, requested);
            if (!found || error < best_error ||
                (error == best_error && reference < plan->reference_divider)) {
                plan->requested_khz = requested;
                plan->achieved_khz = achieved;
                plan->error_ppm = (error / requested) * 1000000UL +
                                  (((error % requested) * 1000UL) / requested) *
                                  1000UL;
                plan->vco_khz = achieved_vco;
                plan->feedback_divider = feedback + 2U;
                plan->reference_divider = reference;
                plan->post_divider = (unsigned int)divider;
                best_error = error;
                found = 1;
            }
        }
    }
    if (!found) {
        *reason = OSMGA_G450_PLL_NO_CANDIDATE;
        return 0;
    }
    *reason = OSMGA_G450_PLL_OK;
    return 1;
}

const char *
OSMGAG450PLLReasonString(OSMGAG450PLLReason reason)
{
    switch (reason) {
    case OSMGA_G450_PLL_OK:
        return "ok";
    case OSMGA_G450_PLL_INVALID_ARGUMENT:
        return "invalid-argument";
    case OSMGA_G450_PLL_R3_MODE:
        return "r3-mode";
    case OSMGA_G450_PLL_SOURCE_UNVERIFIED:
        return "source-unverified";
    case OSMGA_G450_PLL_HEAD_UNVERIFIED:
        return "head-unverified";
    case OSMGA_G450_PLL_HEAD_INVALID:
        return "head-invalid";
    case OSMGA_G450_PLL_NO_CANDIDATE:
        return "no-candidate";
    }
    return "unknown";
}
