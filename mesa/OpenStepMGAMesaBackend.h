/*
 * OpenStepMGAMesaBackend.h - no-hardware backend selection contract.
 *
 * It describes the fixed first Mesa target and decides only between a
 * software fallback and a future hardware candidate.  It never creates an
 * OpenGL context or submits work.
 */

#ifndef OPENSTEP_MGA_MESA_BACKEND_H
#define OPENSTEP_MGA_MESA_BACKEND_H

#define OSMGA_MESA_RENDER_WIDTH 1024U
#define OSMGA_MESA_RENDER_HEIGHT 768U
#define OSMGA_MESA_COLOR_BITS 32U
#define OSMGA_MESA_DEPTH_BITS 16U

typedef struct {
    unsigned int render_width;
    unsigned int render_height;
    unsigned int color_bits_per_pixel;
    unsigned int depth_bits_per_pixel;
    int presentation_contract_verified;
    int r1_sole_owner_verified;
    int r2_physical_profile_verified;
    int r3_mode_record_verified;
    int r4_recovery_verified;
    int mapping_review_verified;
    int render_budget_verified;
    int fence_review_verified;
    int software_fallback_available;
} OSMGAMesaBackendRequest;

typedef enum {
    OSMGA_MESA_BACKEND_SOFTWARE_FALLBACK = 0,
    OSMGA_MESA_BACKEND_HARDWARE_CANDIDATE,
    OSMGA_MESA_BACKEND_REJECTED
} OSMGAMesaBackendDecision;

typedef enum {
    OSMGA_MESA_BACKEND_OK = 0,
    OSMGA_MESA_BACKEND_INVALID_ARGUMENT,
    OSMGA_MESA_BACKEND_FIXED_TARGET_REQUIRED,
    OSMGA_MESA_BACKEND_PRESENTATION_UNVERIFIED,
    OSMGA_MESA_BACKEND_NO_FALLBACK
} OSMGAMesaBackendReason;

/*
 * Return an offline dispatch decision for the fixed 1024x768 RGBA+depth
 * target.  HARDWARE_CANDIDATE is a bookkeeping result, not authorization to
 * access a card; callers still need the separate replacement-only run gate.
 */
int OSMGASelectMesaBackend(const OSMGAMesaBackendRequest *request,
                           OSMGAMesaBackendDecision *decision,
                           OSMGAMesaBackendReason *reason);

const char *OSMGAMesaBackendReasonString(OSMGAMesaBackendReason reason);

#endif /* OPENSTEP_MGA_MESA_BACKEND_H */
