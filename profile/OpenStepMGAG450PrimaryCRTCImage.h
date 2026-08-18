/*
 * OpenStepMGAG450PrimaryCRTCImage.h - data-only primary CRTC byte image.
 *
 * This is an offline encoding of an already accepted 32-bit G450 primary
 * geometry plan. It neither selects an index nor writes a VGA/MMIO register.
 */

#ifndef OPENSTEP_MGA_G450_PRIMARY_CRTC_IMAGE_H
#define OPENSTEP_MGA_G450_PRIMARY_CRTC_IMAGE_H

#include "OpenStepMGAG450CRTCPlan.h"

#define OSMGA_G450_PRIMARY_CRTC_REGISTER_COUNT 25U
#define OSMGA_G450_PRIMARY_EXT_REGISTER_COUNT 6U

typedef struct {
    unsigned char crtc[OSMGA_G450_PRIMARY_CRTC_REGISTER_COUNT];
    unsigned char extended[OSMGA_G450_PRIMARY_EXT_REGISTER_COUNT];
    unsigned char misc_output_or;
} OSMGAG450PrimaryCRTCImage;

typedef enum {
    OSMGA_G450_PRIMARY_CRTC_OK = 0,
    OSMGA_G450_PRIMARY_CRTC_INVALID_ARGUMENT,
    OSMGA_G450_PRIMARY_CRTC_R3_REVIEW,
    OSMGA_G450_PRIMARY_CRTC_GEOMETRY,
    OSMGA_G450_PRIMARY_CRTC_UNSUPPORTED_FORMAT
} OSMGAG450PrimaryCRTCReason;

/* Build the primary-head VGA/extended-CRTC data image, without I/O. */
int OSMGABuildG450PrimaryCRTCImage(const OSMGAR3ManualModeReview *review,
                                   OSMGAG450PrimaryCRTCImage *image,
                                   OSMGAG450PrimaryCRTCReason *reason);

const char *OSMGAG450PrimaryCRTCReasonString(
    OSMGAG450PrimaryCRTCReason reason);

#endif /* OPENSTEP_MGA_G450_PRIMARY_CRTC_IMAGE_H */
