/*
 * OpenStepMGAG450PLLEncoding.h - reviewed G450 PLL byte-image encoding.
 *
 * This contains no port, DAC, MMIO, mapping, or device interface.  Its
 * result is an immutable input for a separately reviewed future writer.
 */

#ifndef OPENSTEP_MGA_G450_PLL_ENCODING_H
#define OPENSTEP_MGA_G450_PLL_ENCODING_H

#include "OpenStepMGAG450PLL.h"

typedef enum {
    OSMGA_G450_PLL_DAC_PRIMARY_PIXEL_C = 0,
    OSMGA_G450_PLL_DAC_SECONDARY_VIDEO
} OSMGAG450PLLDacTarget;

typedef struct {
    OSMGAG450PLLDacTarget target;
    unsigned char m;
    unsigned char n;
    unsigned char p;
} OSMGAG450PLLByteImage;

typedef enum {
    OSMGA_G450_PLL_ENCODING_OK = 0,
    OSMGA_G450_PLL_ENCODING_INVALID_ARGUMENT,
    OSMGA_G450_PLL_ENCODING_INVALID_PLAN,
    OSMGA_G450_PLL_ENCODING_INVALID_HEAD,
    OSMGA_G450_PLL_ENCODING_INVALID_POST_DIVIDER
} OSMGAG450PLLEncodingReason;

/*
 * Encodes one already-reviewed frequency plan.  The function does not write
 * the image and does not choose or retry PLL candidates.
 */
int OSMGAEncodeG450PLLByteImage(const OSMGAG450PLLReview *review,
                                const OSMGAG450PLLPlan *plan,
                                OSMGAG450PLLByteImage *image,
                                OSMGAG450PLLEncodingReason *reason);

const char *OSMGAG450PLLEncodingReasonString(
    OSMGAG450PLLEncodingReason reason);

#endif /* OPENSTEP_MGA_G450_PLL_ENCODING_H */
