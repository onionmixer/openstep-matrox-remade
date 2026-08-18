#include <stdio.h>

#include "OpenStepMGAG450PrimaryCRTCImage.h"

static int failures;

static const unsigned char approved_crtc[25] = {
    0x09U, 0xc7U, 0xc7U, 0x8dU, 0xcfU, 0x07U, 0xe0U, 0x00U,
    0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
    0xb0U, 0x23U, 0xafU, 0x90U, 0x00U, 0xafU, 0xe1U, 0x00U,
    0xafU
};

static const unsigned char approved_extended[6] = {
    0x10U, 0x01U, 0xadU, 0x83U, 0x00U, 0x00U
};

static void
expect(int condition, const char *name)
{
    if (!condition) {
        printf("OPENSTEP_MGA_G450_PRIMARY_CRTC_IMAGE_TEST=fail:%s\n", name);
        failures++;
    }
}

static void
make_review(OSMGAR3ManualModeReview *review)
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

int
main(void)
{
    OSMGAR3ManualModeReview review;
    OSMGAG450PrimaryCRTCImage image;
    OSMGAG450PrimaryCRTCReason reason;
    unsigned int index;

    make_review(&review);
    expect(OSMGABuildG450PrimaryCRTCImage(&review, &image, &reason) == 1,
           "approved-image");
    expect(reason == OSMGA_G450_PRIMARY_CRTC_OK, "approved-reason");
    expect(image.crtc[0] == 0x09U && image.crtc[1] == 0xc7U &&
               image.crtc[4] == 0xcfU && image.crtc[6] == 0xe0U,
           "standard-horizontal-vertical");
    expect(image.crtc[16] == 0xb0U && image.crtc[17] == 0x23U &&
               image.crtc[19] == 0x90U && image.crtc[22] == 0xe1U,
           "standard-sync-pitch");
    expect(image.extended[0] == 0x10U && image.extended[1] == 0x01U &&
               image.extended[2] == 0xadU && image.extended[3] == 0x83U,
           "extended-image");
    expect(image.misc_output_or == 0x0cU, "external-clock-select");
    for (index = 0U; index < 25U; index++) {
        expect(image.crtc[index] == approved_crtc[index],
               "complete-standard-image");
    }
    for (index = 0U; index < 6U; index++) {
        expect(image.extended[index] == approved_extended[index],
               "complete-extended-image");
    }

    make_review(&review);
    review.bits_per_pixel = 16;
    expect(OSMGABuildG450PrimaryCRTCImage(&review, &image, &reason) == 0,
           "16bit-rejected");
    expect(reason == OSMGA_G450_PRIMARY_CRTC_UNSUPPORTED_FORMAT,
           "16bit-reason");

    make_review(&review);
    review.timing.horizontal_front_porch = 65UL;
    expect(OSMGABuildG450PrimaryCRTCImage(&review, &image, &reason) == 0,
           "unaligned-timing-rejected");
    expect(reason == OSMGA_G450_PRIMARY_CRTC_R3_REVIEW,
           "unaligned-timing-reason");

    if (failures != 0) {
        return 1;
    }
    printf("OPENSTEP_MGA_G450_PRIMARY_CRTC_IMAGE_TEST_STATUS=pass\n");
    return 0;
}
