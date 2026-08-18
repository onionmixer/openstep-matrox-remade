#include <stdio.h>

#include "OpenStepMGAG450PLLEncoding.h"

static int failures;

static void
expect(int condition, const char *name)
{
    if (!condition) {
        printf("OPENSTEP_MGA_G450_PLL_ENCODING_TEST=fail:%s\n", name);
        failures++;
    }
}

static void
make_review(OSMGAG450PLLReview *review)
{
    review->head = OSMGA_G450_HEAD_PRIMARY;
}

static void
make_162mhz_plan(OSMGAG450PLLPlan *plan)
{
    plan->requested_khz = 162000UL;
    plan->achieved_khz = 162000UL;
    plan->error_ppm = 0UL;
    plan->vco_khz = 324000UL;
    plan->feedback_divider = 6U;
    plan->reference_divider = 1U;
    plan->post_divider = 2U;
}

int
main(void)
{
    OSMGAG450PLLReview review;
    OSMGAG450PLLPlan plan;
    OSMGAG450PLLByteImage image;
    OSMGAG450PLLEncodingReason reason;

    make_review(&review);
    make_162mhz_plan(&plan);
    expect(OSMGAEncodeG450PLLByteImage(&review, &plan, &image, &reason) == 1,
           "primary-162mhz");
    expect(reason == OSMGA_G450_PLL_ENCODING_OK, "primary-reason");
    expect(image.target == OSMGA_G450_PLL_DAC_PRIMARY_PIXEL_C,
           "primary-target");
    expect(image.m == 0U && image.n == 4U && image.p == 0U,
           "primary-exact-byte-image");

    review.head = OSMGA_G450_HEAD_SECONDARY;
    expect(OSMGAEncodeG450PLLByteImage(&review, &plan, &image, &reason) == 1,
           "secondary-162mhz");
    expect(image.target == OSMGA_G450_PLL_DAC_SECONDARY_VIDEO,
           "secondary-target");
    expect(image.m == 0U && image.n == 4U && image.p == 0U,
           "secondary-same-byte-image");

    review.head = OSMGA_G450_HEAD_UNKNOWN;
    expect(OSMGAEncodeG450PLLByteImage(&review, &plan, &image, &reason) == 0,
           "unknown-head-rejected");
    expect(reason == OSMGA_G450_PLL_ENCODING_INVALID_HEAD,
           "unknown-head-reason");

    make_review(&review);
    plan.post_divider = 3U;
    expect(OSMGAEncodeG450PLLByteImage(&review, &plan, &image, &reason) == 0,
           "invalid-divider-rejected");
    expect(reason == OSMGA_G450_PLL_ENCODING_INVALID_POST_DIVIDER,
           "invalid-divider-reason");

    make_162mhz_plan(&plan);
    plan.vco_khz = 1300001UL;
    expect(OSMGAEncodeG450PLLByteImage(&review, &plan, &image, &reason) == 0,
           "vco-range-rejected");
    expect(reason == OSMGA_G450_PLL_ENCODING_INVALID_PLAN,
           "vco-range-reason");

    expect(OSMGAG450PLLEncodingReasonString(
               OSMGA_G450_PLL_ENCODING_OK)[0] == 'o', "reason-string");
    if (failures != 0) {
        return 1;
    }
    printf("OPENSTEP_MGA_G450_PLL_ENCODING_TEST_STATUS=pass\n");
    return 0;
}
