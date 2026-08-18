/*
 * OpenStepMGAG450CRTCPlan.h - register-free geometry plan for one G450 mode.
 *
 * This is deliberately not a register image or a programming sequence.  It
 * derives only checked visible/blanking boundaries for a later, separately
 * reviewed recovery-only writer.
 */

#ifndef OPENSTEP_MGA_G450_CRTC_PLAN_H
#define OPENSTEP_MGA_G450_CRTC_PLAN_H

#include "OpenStepMGAModeReview.h"

typedef struct {
    unsigned long pixel_clock_khz;
    unsigned long horizontal_display;
    unsigned long horizontal_sync_start;
    unsigned long horizontal_sync_end;
    unsigned long horizontal_total;
    unsigned long vertical_display;
    unsigned long vertical_sync_start;
    unsigned long vertical_sync_end;
    unsigned long vertical_total;
    unsigned long pitch_bytes;
    unsigned long scanout_bytes;
    int hsync_positive;
    int vsync_positive;
} OSMGAG450CRTCPlan;

typedef enum {
    OSMGA_G450_CRTC_PLAN_OK = 0,
    OSMGA_G450_CRTC_PLAN_INVALID_ARGUMENT,
    OSMGA_G450_CRTC_PLAN_R3_REVIEW,
    OSMGA_G450_CRTC_PLAN_UNSUPPORTED_FORMAT,
    OSMGA_G450_CRTC_PLAN_OVERFLOW
} OSMGAG450CRTCPlanReason;

int OSMGABuildG450CRTCPlan(const OSMGAR3ManualModeReview *review,
                           OSMGAG450CRTCPlan *plan,
                           OSMGAG450CRTCPlanReason *reason);

const char *OSMGAG450CRTCPlanReasonString(OSMGAG450CRTCPlanReason reason);

#endif /* OPENSTEP_MGA_G450_CRTC_PLAN_H */
