/* Pure-C primary CRTC snapshot comparison; no target interfaces. */

#include "OpenStepMGAG450PrimaryCRTCReadback.h"

int
OSMGAValidateG450PrimaryCRTCReadback(
    const OSMGAG450PrimaryCRTCImage *expected,
    const OSMGAG450PrimaryCRTCReadback *observed,
    OSMGAG450PrimaryCRTCReadbackReason *reason)
{
    unsigned int index;

    if (reason == 0) {
        return 0;
    }
    if (expected == 0 || observed == 0) {
        *reason = OSMGA_G450_PRIMARY_CRTC_READBACK_INVALID_ARGUMENT;
        return 0;
    }
    for (index = 0U; index < OSMGA_G450_PRIMARY_CRTC_REGISTER_COUNT; index++) {
        if (observed->crtc[index] != expected->crtc[index]) {
            *reason = OSMGA_G450_PRIMARY_CRTC_READBACK_STANDARD_MISMATCH;
            return 0;
        }
    }
    for (index = 0U; index < OSMGA_G450_PRIMARY_EXT_REGISTER_COUNT; index++) {
        if (observed->extended[index] != expected->extended[index]) {
            *reason = OSMGA_G450_PRIMARY_CRTC_READBACK_EXTENDED_MISMATCH;
            return 0;
        }
    }
    if ((observed->misc_output & expected->misc_output_or) !=
        expected->misc_output_or) {
        *reason = OSMGA_G450_PRIMARY_CRTC_READBACK_MISC_CLOCK_MISMATCH;
        return 0;
    }
    *reason = OSMGA_G450_PRIMARY_CRTC_READBACK_OK;
    return 1;
}

const char *
OSMGAG450PrimaryCRTCReadbackReasonString(
    OSMGAG450PrimaryCRTCReadbackReason reason)
{
    switch (reason) {
    case OSMGA_G450_PRIMARY_CRTC_READBACK_OK:
        return "ok";
    case OSMGA_G450_PRIMARY_CRTC_READBACK_INVALID_ARGUMENT:
        return "invalid-argument";
    case OSMGA_G450_PRIMARY_CRTC_READBACK_STANDARD_MISMATCH:
        return "standard-mismatch";
    case OSMGA_G450_PRIMARY_CRTC_READBACK_EXTENDED_MISMATCH:
        return "extended-mismatch";
    case OSMGA_G450_PRIMARY_CRTC_READBACK_MISC_CLOCK_MISMATCH:
        return "misc-clock-mismatch";
    }
    return "unknown";
}
