/* Data-only comparison of a caller-supplied primary CRTC snapshot. */

#ifndef OPENSTEP_MGA_G450_PRIMARY_CRTC_READBACK_H
#define OPENSTEP_MGA_G450_PRIMARY_CRTC_READBACK_H

#include "OpenStepMGAG450PrimaryCRTCImage.h"

typedef struct {
    unsigned char crtc[OSMGA_G450_PRIMARY_CRTC_REGISTER_COUNT];
    unsigned char extended[OSMGA_G450_PRIMARY_EXT_REGISTER_COUNT];
    unsigned char misc_output;
} OSMGAG450PrimaryCRTCReadback;

typedef enum {
    OSMGA_G450_PRIMARY_CRTC_READBACK_OK = 0,
    OSMGA_G450_PRIMARY_CRTC_READBACK_INVALID_ARGUMENT,
    OSMGA_G450_PRIMARY_CRTC_READBACK_STANDARD_MISMATCH,
    OSMGA_G450_PRIMARY_CRTC_READBACK_EXTENDED_MISMATCH,
    OSMGA_G450_PRIMARY_CRTC_READBACK_MISC_CLOCK_MISMATCH
} OSMGAG450PrimaryCRTCReadbackReason;

/* Compare caller-obtained bytes; no device access is performed here. */
int OSMGAValidateG450PrimaryCRTCReadback(
    const OSMGAG450PrimaryCRTCImage *expected,
    const OSMGAG450PrimaryCRTCReadback *observed,
    OSMGAG450PrimaryCRTCReadbackReason *reason);

const char *OSMGAG450PrimaryCRTCReadbackReasonString(
    OSMGAG450PrimaryCRTCReadbackReason reason);

#endif /* OPENSTEP_MGA_G450_PRIMARY_CRTC_READBACK_H */
