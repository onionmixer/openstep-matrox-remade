/* No-hardware Mesa backend selection contract. */

#include "OpenStepMGAMesaBackend.h"

static int
hardware_candidate_ready(const OSMGAMesaBackendRequest *request)
{
    return request->r1_sole_owner_verified &&
           request->r2_physical_profile_verified &&
           request->r3_mode_record_verified &&
           request->r4_recovery_verified &&
           request->mapping_review_verified &&
           request->render_budget_verified &&
           request->fence_review_verified;
}

int
OSMGASelectMesaBackend(const OSMGAMesaBackendRequest *request,
                       OSMGAMesaBackendDecision *decision,
                       OSMGAMesaBackendReason *reason)
{
    if (reason == 0 || decision == 0) {
        return 0;
    }
    if (request == 0) {
        *reason = OSMGA_MESA_BACKEND_INVALID_ARGUMENT;
        return 0;
    }
    if (request->render_width != OSMGA_MESA_RENDER_WIDTH ||
        request->render_height != OSMGA_MESA_RENDER_HEIGHT ||
        request->color_bits_per_pixel != OSMGA_MESA_COLOR_BITS ||
        request->depth_bits_per_pixel != OSMGA_MESA_DEPTH_BITS) {
        *decision = OSMGA_MESA_BACKEND_REJECTED;
        *reason = OSMGA_MESA_BACKEND_FIXED_TARGET_REQUIRED;
        return 0;
    }
    if (!request->presentation_contract_verified) {
        *decision = OSMGA_MESA_BACKEND_REJECTED;
        *reason = OSMGA_MESA_BACKEND_PRESENTATION_UNVERIFIED;
        return 0;
    }
    if (hardware_candidate_ready(request)) {
        *decision = OSMGA_MESA_BACKEND_HARDWARE_CANDIDATE;
        *reason = OSMGA_MESA_BACKEND_OK;
        return 1;
    }
    if (!request->software_fallback_available) {
        *decision = OSMGA_MESA_BACKEND_REJECTED;
        *reason = OSMGA_MESA_BACKEND_NO_FALLBACK;
        return 0;
    }
    *decision = OSMGA_MESA_BACKEND_SOFTWARE_FALLBACK;
    *reason = OSMGA_MESA_BACKEND_OK;
    return 1;
}

const char *
OSMGAMesaBackendReasonString(OSMGAMesaBackendReason reason)
{
    switch (reason) {
    case OSMGA_MESA_BACKEND_OK:
        return "ok";
    case OSMGA_MESA_BACKEND_INVALID_ARGUMENT:
        return "invalid-argument";
    case OSMGA_MESA_BACKEND_FIXED_TARGET_REQUIRED:
        return "fixed-target-required";
    case OSMGA_MESA_BACKEND_PRESENTATION_UNVERIFIED:
        return "presentation-unverified";
    case OSMGA_MESA_BACKEND_NO_FALLBACK:
        return "no-fallback";
    default:
        return "unknown";
    }
}
