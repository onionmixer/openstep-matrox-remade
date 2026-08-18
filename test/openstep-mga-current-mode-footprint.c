/* Current production Display Mode arithmetic only; no target interfaces. */

#include <stdio.h>

#include "OpenStepMGAEDID.h"

int
main(void)
{
    OSMGAMode mode;
    OSMGAModeMemoryReason reason;
    unsigned long required;
    unsigned long profile_bytes;

    if (!OSMGAParseManualDisplayMode(
            "Height: 1200 Width: 1600 Refresh: 60Hz ColorSpace: RGB:888/32",
            &mode)) {
        printf("OPENSTEP_MGA_CURRENT_MODE_PARSE=fail\n");
        return 1;
    }

    profile_bytes = 16UL * 1024UL * 1024UL;
    required = 0;
    if (!OSMGAModeFitsLinearMemory(&mode, 32, 6400UL, profile_bytes,
                                   &required, &reason)) {
        printf("OPENSTEP_MGA_CURRENT_MODE_FOOTPRINT=fail reason=%d\n",
               (int)reason);
        return 1;
    }

    printf("OPENSTEP_MGA_CURRENT_MODE_PARSE=pass width=%u height=%u refresh_millihz=%lu\n",
           (unsigned int)mode.width, (unsigned int)mode.height,
           mode.refresh_millihz);
    printf("OPENSTEP_MGA_CURRENT_MODE_FOOTPRINT=pass required_bytes=%lu profile_bytes=%lu profile_headroom=%lu\n",
           required, profile_bytes, profile_bytes - required);
    printf("OPENSTEP_MGA_CURRENT_MODE_INTERPRETATION=profile-lower-bound-not-offscreen-proof\n");
    return 0;
}
