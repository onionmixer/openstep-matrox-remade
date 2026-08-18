/* Pure-C G450 PLL byte-image encoding; no target interfaces. */

#include "OpenStepMGAG450PLLEncoding.h"

#define OSMGA_G450_PLL_VCO_MIN_KHZ 256000UL
#define OSMGA_G450_PLL_VCO_MAX_KHZ 1300000UL

static int
post_divider_bits(unsigned int post_divider, unsigned char *bits)
{
    switch (post_divider) {
    case 1U:
        *bits = 0x40U;
        return 1;
    case 2U:
        *bits = 0x00U;
        return 1;
    case 4U:
        *bits = 0x01U;
        return 1;
    case 8U:
        *bits = 0x02U;
        return 1;
    case 16U:
        *bits = 0x03U;
        return 1;
    }
    return 0;
}

static unsigned char
stability_bits(unsigned long vco_khz)
{
    if (vco_khz < 550000UL) {
        return 0x00U;
    }
    if (vco_khz < 700000UL) {
        return 0x08U;
    }
    if (vco_khz < 900000UL) {
        return 0x10U;
    }
    if (vco_khz < 1100000UL) {
        return 0x18U;
    }
    if (vco_khz < 1300000UL) {
        return 0x20U;
    }
    return 0x28U;
}

int
OSMGAEncodeG450PLLByteImage(const OSMGAG450PLLReview *review,
                            const OSMGAG450PLLPlan *plan,
                            OSMGAG450PLLByteImage *image,
                            OSMGAG450PLLEncodingReason *reason)
{
    unsigned char divider;

    if (reason == 0 || image == 0) {
        return 0;
    }
    if (review == 0 || plan == 0) {
        *reason = OSMGA_G450_PLL_ENCODING_INVALID_ARGUMENT;
        return 0;
    }
    if (plan->requested_khz == 0UL || plan->achieved_khz == 0UL ||
        plan->vco_khz < OSMGA_G450_PLL_VCO_MIN_KHZ ||
        plan->vco_khz > OSMGA_G450_PLL_VCO_MAX_KHZ ||
        plan->feedback_divider < 2U || plan->feedback_divider > 257U ||
        plan->reference_divider == 0U || plan->reference_divider > 256U) {
        *reason = OSMGA_G450_PLL_ENCODING_INVALID_PLAN;
        return 0;
    }
    if (review->head != OSMGA_G450_HEAD_PRIMARY &&
        review->head != OSMGA_G450_HEAD_SECONDARY) {
        *reason = OSMGA_G450_PLL_ENCODING_INVALID_HEAD;
        return 0;
    }
    if (!post_divider_bits(plan->post_divider, &divider)) {
        *reason = OSMGA_G450_PLL_ENCODING_INVALID_POST_DIVIDER;
        return 0;
    }

    image->target = review->head == OSMGA_G450_HEAD_PRIMARY ?
        OSMGA_G450_PLL_DAC_PRIMARY_PIXEL_C :
        OSMGA_G450_PLL_DAC_SECONDARY_VIDEO;
    image->m = (unsigned char)(plan->reference_divider - 1U);
    image->n = (unsigned char)(plan->feedback_divider - 2U);
    image->p = (unsigned char)(divider | stability_bits(plan->vco_khz));
    *reason = OSMGA_G450_PLL_ENCODING_OK;
    return 1;
}

const char *
OSMGAG450PLLEncodingReasonString(OSMGAG450PLLEncodingReason reason)
{
    switch (reason) {
    case OSMGA_G450_PLL_ENCODING_OK:
        return "ok";
    case OSMGA_G450_PLL_ENCODING_INVALID_ARGUMENT:
        return "invalid-argument";
    case OSMGA_G450_PLL_ENCODING_INVALID_PLAN:
        return "invalid-plan";
    case OSMGA_G450_PLL_ENCODING_INVALID_HEAD:
        return "invalid-head";
    case OSMGA_G450_PLL_ENCODING_INVALID_POST_DIVIDER:
        return "invalid-post-divider";
    }
    return "unknown";
}
