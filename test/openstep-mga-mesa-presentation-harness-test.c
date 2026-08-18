#include <stdio.h>

#include "OpenStepMGAMesaBackend.h"
#include "OpenStepMGAReference.h"

static int failures;

static void
expect(int condition, const char *name)
{
    if (!condition) {
        printf("OPENSTEP_MGA_MESA_PRESENTATION_HARNESS=fail:%s\n", name);
        failures++;
    }
}

int
main(void)
{
    OSMGAMesaBackendRequest request;
    OSMGAMesaBackendDecision decision;
    OSMGAMesaBackendReason reason;
    unsigned long render[4];
    unsigned long desktop[9];
    unsigned int index;

    request.render_width = OSMGA_MESA_RENDER_WIDTH;
    request.render_height = OSMGA_MESA_RENDER_HEIGHT;
    request.color_bits_per_pixel = OSMGA_MESA_COLOR_BITS;
    request.depth_bits_per_pixel = OSMGA_MESA_DEPTH_BITS;
    request.presentation_contract_verified = 1;
    request.r1_sole_owner_verified = 0;
    request.r2_physical_profile_verified = 0;
    request.r3_mode_record_verified = 0;
    request.r4_recovery_verified = 0;
    request.mapping_review_verified = 0;
    request.render_budget_verified = 0;
    request.fence_review_verified = 0;
    request.software_fallback_available = 1;
    expect(OSMGASelectMesaBackend(&request, &decision, &reason) == 1,
           "fallback-dispatch");
    expect(decision == OSMGA_MESA_BACKEND_SOFTWARE_FALLBACK,
           "fallback-dispatch-decision");

    render[0] = 0xff000001UL;
    render[1] = 0xff000002UL;
    render[2] = 0xff000003UL;
    render[3] = 0xff000004UL;
    for (index = 0; index < 9; index++) {
        desktop[index] = 0;
    }
    expect(OSMGAReferenceScaleNearest32(render, 2, 2, 2,
                                        desktop, 3, 3, 3) == 1,
           "fallback-scale");
    expect(desktop[0] == 0xff000001UL && desktop[1] == 0xff000001UL &&
           desktop[2] == 0xff000002UL && desktop[3] == 0xff000001UL &&
           desktop[4] == 0xff000001UL && desktop[5] == 0xff000002UL &&
           desktop[6] == 0xff000003UL && desktop[7] == 0xff000003UL &&
           desktop[8] == 0xff000004UL, "fallback-scale-values");

    if (failures != 0) {
        return 1;
    }
    printf("OPENSTEP_MGA_MESA_PRESENTATION_HARNESS_STATUS=pass\n");
    return 0;
}
