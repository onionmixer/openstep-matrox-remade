/*
 * OpenStepMGAMesaAdmission.h - bind reviewed R3/R6 records to Mesa dispatch.
 *
 * The result remains an offline policy record.  It does not create a context,
 * map memory, program a mode, or touch a device.
 */

#ifndef OPENSTEP_MGA_MESA_ADMISSION_H
#define OPENSTEP_MGA_MESA_ADMISSION_H

#include "OpenStepMGAMesaBackend.h"
#include "OpenStepMGAMappingReview.h"
#include "OpenStepMGARenderBudget.h"

typedef enum {
    OSMGA_MESA_ADMISSION_OK = 0,
    OSMGA_MESA_ADMISSION_INVALID_ARGUMENT,
    OSMGA_MESA_ADMISSION_R6_MAPPING_REVIEW,
    OSMGA_MESA_ADMISSION_R3_MODE_REVIEW,
    OSMGA_MESA_ADMISSION_FIXED_TARGET_REQUIRED,
    OSMGA_MESA_ADMISSION_PROFILE_BUDGET_MISMATCH,
    OSMGA_MESA_ADMISSION_SCANOUT_BUDGET_MISMATCH,
    OSMGA_MESA_ADMISSION_ALIGNMENT_MISMATCH,
    OSMGA_MESA_ADMISSION_RENDER_BUDGET
} OSMGAMesaAdmissionReason;

/*
 * Compose a fixed-target Mesa request only from an accepted R6 record and a
 * render budget tied to that record's exact R2/R3 quantities.  Presentation,
 * fence and software-fallback facts remain explicit caller attestations.
 */
int OSMGABuildMesaBackendRequest(const OSMGAR6MappingReview *mapping_review,
                                 const OSMGARenderBudgetRequest *budget_request,
                                 int presentation_contract_verified,
                                 int fence_review_verified,
                                 int software_fallback_available,
                                 OSMGAMesaBackendRequest *backend_request,
                                 OSMGARenderBudgetResult *budget_result,
                                 OSMGAMesaAdmissionReason *reason);

const char *OSMGAMesaAdmissionReasonString(OSMGAMesaAdmissionReason reason);

#endif /* OPENSTEP_MGA_MESA_ADMISSION_H */
