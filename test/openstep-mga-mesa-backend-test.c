#include <stdio.h>

#include "OpenStepMGAMesaBackend.h"

static int failures;

static void
expect(int condition, const char *name)
{
    if (!condition) {
        printf("OPENSTEP_MGA_MESA_BACKEND_TEST=fail:%s\n", name);
        failures++;
    }
}

static void
make_request(OSMGAMesaBackendRequest *request)
{
    request->render_width = OSMGA_MESA_RENDER_WIDTH;
    request->render_height = OSMGA_MESA_RENDER_HEIGHT;
    request->color_bits_per_pixel = OSMGA_MESA_COLOR_BITS;
    request->depth_bits_per_pixel = OSMGA_MESA_DEPTH_BITS;
    request->presentation_contract_verified = 1;
    request->r1_sole_owner_verified = 0;
    request->r2_physical_profile_verified = 0;
    request->r3_mode_record_verified = 0;
    request->r4_recovery_verified = 0;
    request->mapping_review_verified = 0;
    request->render_budget_verified = 0;
    request->fence_review_verified = 0;
    request->software_fallback_available = 1;
}

int
main(void)
{
    OSMGAMesaBackendRequest request;
    OSMGAMesaBackendDecision decision;
    OSMGAMesaBackendReason reason;

    expect(OSMGASelectMesaBackend(0, &decision, &reason) == 0,
           "null-request-rejected");
    expect(reason == OSMGA_MESA_BACKEND_INVALID_ARGUMENT,
           "null-request-reason");

    make_request(&request);
    expect(OSMGASelectMesaBackend(&request, &decision, &reason) == 1,
           "software-fallback-selected");
    expect(decision == OSMGA_MESA_BACKEND_SOFTWARE_FALLBACK,
           "software-fallback-decision");
    expect(reason == OSMGA_MESA_BACKEND_OK, "software-fallback-reason");

    make_request(&request);
    request.render_width = 800;
    expect(OSMGASelectMesaBackend(&request, &decision, &reason) == 0,
           "fixed-target-enforced");
    expect(decision == OSMGA_MESA_BACKEND_REJECTED,
           "fixed-target-rejected-decision");
    expect(reason == OSMGA_MESA_BACKEND_FIXED_TARGET_REQUIRED,
           "fixed-target-reason");

    make_request(&request);
    request.presentation_contract_verified = 0;
    expect(OSMGASelectMesaBackend(&request, &decision, &reason) == 0,
           "presentation-required");
    expect(reason == OSMGA_MESA_BACKEND_PRESENTATION_UNVERIFIED,
           "presentation-required-reason");

    make_request(&request);
    request.software_fallback_available = 0;
    expect(OSMGASelectMesaBackend(&request, &decision, &reason) == 0,
           "fallback-required");
    expect(reason == OSMGA_MESA_BACKEND_NO_FALLBACK,
           "fallback-required-reason");

    make_request(&request);
    request.r1_sole_owner_verified = 1;
    request.r2_physical_profile_verified = 1;
    request.r3_mode_record_verified = 1;
    request.r4_recovery_verified = 1;
    request.mapping_review_verified = 1;
    request.fence_review_verified = 1;
    expect(OSMGASelectMesaBackend(&request, &decision, &reason) == 1,
           "budget-required-for-hardware");
    expect(decision == OSMGA_MESA_BACKEND_SOFTWARE_FALLBACK,
           "budget-required-fallback-decision");

    request.render_budget_verified = 1;
    expect(OSMGASelectMesaBackend(&request, &decision, &reason) == 1,
           "hardware-candidate-selected");
    expect(decision == OSMGA_MESA_BACKEND_HARDWARE_CANDIDATE,
           "hardware-candidate-decision");
    expect(OSMGAMesaBackendReasonString(reason)[0] == 'o', "reason-string");

    if (failures != 0) {
        return 1;
    }
    printf("OPENSTEP_MGA_MESA_BACKEND_TEST_STATUS=pass\n");
    return 0;
}
