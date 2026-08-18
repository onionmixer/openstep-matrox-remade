/* Pure-C primary G450 CRTC encoding; no target interfaces. */

#include "OpenStepMGAG450PrimaryCRTCImage.h"

int
OSMGABuildG450PrimaryCRTCImage(const OSMGAR3ManualModeReview *review,
                               OSMGAG450PrimaryCRTCImage *image,
                               OSMGAG450PrimaryCRTCReason *reason)
{
    OSMGAG450CRTCPlan plan;
    OSMGAG450CRTCPlanReason plan_reason;
    unsigned long hd;
    unsigned long hs;
    unsigned long he;
    unsigned long ht;
    unsigned long vd;
    unsigned long vs;
    unsigned long ve;
    unsigned long vt;
    unsigned long wd;
    unsigned int index;

    if (reason == 0 || image == 0) {
        return 0;
    }
    if (review == 0) {
        *reason = OSMGA_G450_PRIMARY_CRTC_INVALID_ARGUMENT;
        return 0;
    }
    if (!OSMGABuildG450CRTCPlan(review, &plan, &plan_reason)) {
        *reason = plan_reason == OSMGA_G450_CRTC_PLAN_UNSUPPORTED_FORMAT ?
            OSMGA_G450_PRIMARY_CRTC_UNSUPPORTED_FORMAT :
            OSMGA_G450_PRIMARY_CRTC_R3_REVIEW;
        return 0;
    }
    if (review->bits_per_pixel != 32U) {
        *reason = OSMGA_G450_PRIMARY_CRTC_UNSUPPORTED_FORMAT;
        return 0;
    }
    if ((plan.horizontal_display & 7UL) != 0UL ||
        (plan.horizontal_sync_start & 7UL) != 0UL ||
        (plan.horizontal_sync_end & 7UL) != 0UL ||
        (plan.horizontal_total & 7UL) != 0UL ||
        plan.vertical_display == 0UL || plan.vertical_sync_start == 0UL ||
        plan.vertical_sync_end == 0UL || plan.vertical_total < 2UL ||
        (plan.pitch_bytes & 15UL) != 0UL) {
        *reason = OSMGA_G450_PRIMARY_CRTC_GEOMETRY;
        return 0;
    }

    hd = (plan.horizontal_display >> 3) - 1UL;
    hs = (plan.horizontal_sync_start >> 3) - 1UL;
    he = (plan.horizontal_sync_end >> 3) - 1UL;
    ht = (plan.horizontal_total >> 3) - 1UL;
    vd = plan.vertical_display - 1UL;
    vs = plan.vertical_sync_start - 1UL;
    ve = plan.vertical_sync_end - 1UL;
    vt = plan.vertical_total - 2UL;
    wd = plan.horizontal_display >> 2;

    if ((ht & 7UL) == 6UL || (ht & 7UL) == 4UL) {
        ht++;
    }
    /*
     * Several CRTC quantities deliberately exceed one byte: their upper
     * bits are encoded in the overflow/extended image below.  Validate the
     * representable field widths, rather than rejecting such values merely
     * because the low-byte registers truncate them.
     */
    if (hd > 0x1ffUL || hs > 0x1ffUL || he > 0x1ffUL ||
        ht < 4UL || ht > 0x1ffUL || wd > 0x3ffUL ||
        vd > 0xfffUL || vs > 0xfffUL || ve > 0xfffUL ||
        vt > 0xfffUL) {
        *reason = OSMGA_G450_PRIMARY_CRTC_GEOMETRY;
        return 0;
    }

    for (index = 0U; index < OSMGA_G450_PRIMARY_CRTC_REGISTER_COUNT; index++) {
        image->crtc[index] = 0U;
    }
    image->extended[0] = (unsigned char)((wd & 0x300UL) >> 4);
    image->extended[1] = (unsigned char)((((ht - 4UL) & 0x100UL) >> 8) |
                                         ((hd & 0x100UL) >> 7) |
                                         ((hs & 0x100UL) >> 6) |
                                         (ht & 0x40UL));
    image->extended[2] = (unsigned char)(((vt & 0xc00UL) >> 10) |
                                         ((vd & 0x400UL) >> 8) |
                                         ((vd & 0xc00UL) >> 7) |
                                         ((vs & 0xc00UL) >> 5) |
                                         ((vd & 0x400UL) >> 3));
    image->extended[3] = 0x83U;
    image->extended[4] = 0U;
    image->extended[5] = 0U;

    image->crtc[0] = (unsigned char)(ht - 4UL);
    image->crtc[1] = (unsigned char)hd;
    image->crtc[2] = (unsigned char)hd;
    image->crtc[3] = (unsigned char)((ht & 0x1fUL) | 0x80UL);
    image->crtc[4] = (unsigned char)hs;
    image->crtc[5] = (unsigned char)(((ht & 0x20UL) << 2) | (he & 0x1fUL));
    image->crtc[6] = (unsigned char)vt;
    image->crtc[7] = (unsigned char)(((vt & 0x100UL) >> 8) |
                                     ((vd & 0x100UL) >> 7) |
                                     ((vs & 0x100UL) >> 6) |
                                     ((vd & 0x100UL) >> 5) |
                                     ((vd & 0x100UL) >> 4) |
                                     ((vt & 0x200UL) >> 4) |
                                     ((vd & 0x200UL) >> 3) |
                                     ((vs & 0x200UL) >> 2));
    image->crtc[9] = (unsigned char)(((vd & 0x200UL) >> 4) |
                                     ((vd & 0x200UL) >> 3));
    image->crtc[16] = (unsigned char)vs;
    image->crtc[17] = (unsigned char)((ve & 0x0fUL) | 0x20UL);
    image->crtc[18] = (unsigned char)vd;
    image->crtc[19] = (unsigned char)wd;
    image->crtc[21] = (unsigned char)vd;
    image->crtc[22] = (unsigned char)(vt + 1UL);
    image->crtc[24] = (unsigned char)vd;
    image->misc_output_or = 0x0cU;
    *reason = OSMGA_G450_PRIMARY_CRTC_OK;
    return 1;
}

const char *
OSMGAG450PrimaryCRTCReasonString(OSMGAG450PrimaryCRTCReason reason)
{
    switch (reason) {
    case OSMGA_G450_PRIMARY_CRTC_OK:
        return "ok";
    case OSMGA_G450_PRIMARY_CRTC_INVALID_ARGUMENT:
        return "invalid-argument";
    case OSMGA_G450_PRIMARY_CRTC_R3_REVIEW:
        return "r3-review";
    case OSMGA_G450_PRIMARY_CRTC_GEOMETRY:
        return "geometry";
    case OSMGA_G450_PRIMARY_CRTC_UNSUPPORTED_FORMAT:
        return "unsupported-format";
    }
    return "unknown";
}
