/*
 * OpenStepMGAModeTransaction.h - offline R6 one-mode transaction state.
 *
 * This policy composes evidence validators and sampled outcomes.  It never
 * maps a range, changes a display mode, or reads/writes a device register.
 */

#ifndef OPENSTEP_MGA_MODE_TRANSACTION_H
#define OPENSTEP_MGA_MODE_TRANSACTION_H

#include "OpenStepMGABoundedPoll.h"
#include "OpenStepMGAG450CRTCPlan.h"
#include "OpenStepMGAG450PrimaryCRTCImage.h"
#include "OpenStepMGAG450PrimaryCRTCReadback.h"
#include "OpenStepMGAG450PLLEncoding.h"
#include "OpenStepMGAG450PLL.h"
#include "OpenStepMGAG450RangePlan.h"
#include "OpenStepMGAMappingReview.h"
#include "OpenStepMGARecoveryMatrix.h"

typedef enum {
    OSMGA_MODE_TRANSACTION_IDLE = 0,
    OSMGA_MODE_TRANSACTION_PREFLIGHT_READY,
    OSMGA_MODE_TRANSACTION_PLL_LOCK_PENDING,
    OSMGA_MODE_TRANSACTION_PLL_LOCKED,
    OSMGA_MODE_TRANSACTION_PRIMARY_CRTC_VERIFIED,
    OSMGA_MODE_TRANSACTION_LINEAR_ACTIVE,
    OSMGA_MODE_TRANSACTION_ROLLBACK_REQUIRED,
    OSMGA_MODE_TRANSACTION_ROLLED_BACK
} OSMGAModeTransactionState;

typedef struct {
    OSMGAModeTransactionState state;
    unsigned int rollback_restore_mask;
    OSMGABoundedPollState pll_lock;
    OSMGAG450CRTCPlan crtc_plan;
    OSMGAG450PrimaryCRTCImage primary_crtc_image;
    OSMGAG450PLLPlan pll_plan;
    OSMGAG450PLLByteImage pll_byte_image;
    OSMGAG450RangePlan range_plan;
} OSMGAModeTransaction;

typedef enum {
    OSMGA_MODE_TRANSACTION_OK = 0,
    OSMGA_MODE_TRANSACTION_INVALID_ARGUMENT,
    OSMGA_MODE_TRANSACTION_MAPPING_REVIEW,
    OSMGA_MODE_TRANSACTION_RECOVERY_MATRIX,
    OSMGA_MODE_TRANSACTION_PLL_REVIEW,
    OSMGA_MODE_TRANSACTION_CRTC_PLAN,
    OSMGA_MODE_TRANSACTION_PRIMARY_CRTC_IMAGE,
    OSMGA_MODE_TRANSACTION_PRIMARY_CRTC_READBACK,
    OSMGA_MODE_TRANSACTION_PLL_ENCODING,
    OSMGA_MODE_TRANSACTION_RANGE_PLAN,
    OSMGA_MODE_TRANSACTION_REVIEW_MISMATCH,
    OSMGA_MODE_TRANSACTION_POLL_POLICY,
    OSMGA_MODE_TRANSACTION_INVALID_STATE,
    OSMGA_MODE_TRANSACTION_PLL_TIMEOUT,
    OSMGA_MODE_TRANSACTION_LINEAR_FAILURE,
    OSMGA_MODE_TRANSACTION_ROLLBACK_STAGE_FAILURE,
    OSMGA_MODE_TRANSACTION_ROLLBACK_INCOMPLETE
} OSMGAModeTransactionReason;

typedef enum {
    OSMGA_ROLLBACK_STAGE_DISPLAY_STATE = 1U,
    OSMGA_ROLLBACK_STAGE_PLL_STATE = 2U,
    OSMGA_ROLLBACK_STAGE_VGA_SAFE_STATE = 4U,
    OSMGA_ROLLBACK_STAGE_SUPERCLASS_REVERT = 8U
} OSMGAModeRollbackStage;

#define OSMGA_ROLLBACK_STAGE_ALL \
    (OSMGA_ROLLBACK_STAGE_DISPLAY_STATE | OSMGA_ROLLBACK_STAGE_PLL_STATE | \
     OSMGA_ROLLBACK_STAGE_VGA_SAFE_STATE | OSMGA_ROLLBACK_STAGE_SUPERCLASS_REVERT)

/* Validate the full offline preflight and create a preflight-ready state. */
int OSMGABeginModeTransaction(const OSMGAR6MappingReview *mapping_review,
                              const OSMGARecoveryMatrix *recovery_matrix,
                              const OSMGAG450PLLReview *pll_review,
                              unsigned long framebuffer_physical_start,
                              const OSMGABoundedPollPolicy *pll_lock_policy,
                              OSMGAModeTransaction *transaction,
                              OSMGAModeTransactionReason *reason);

int OSMGABeginPLLLock(OSMGAModeTransaction *transaction,
                      OSMGAModeTransactionReason *reason);

OSMGAModeTransactionReason OSMGAObservePLLLock(
    const OSMGABoundedPollPolicy *pll_lock_policy,
    OSMGAModeTransaction *transaction, unsigned long elapsed_msec, int locked);

/* Record only the result of a future caller's CRTC snapshot comparison. */
OSMGAModeTransactionReason OSMGAReportPrimaryCRTCReadback(
    OSMGAModeTransaction *transaction,
    const OSMGAG450PrimaryCRTCReadback *observed);

/* Record only the result of a future caller's linear-mode operation. */
OSMGAModeTransactionReason OSMGAReportLinearModeEntry(
    OSMGAModeTransaction *transaction, int succeeded);

int OSMGARequireModeRollback(OSMGAModeTransaction *transaction,
                             OSMGAModeTransactionReason *reason);
OSMGAModeTransactionReason OSMGAReportModeRollbackStage(
    OSMGAModeTransaction *transaction, OSMGAModeRollbackStage stage,
    int restored);
int OSMGACompleteModeRollback(OSMGAModeTransaction *transaction,
                              OSMGAModeTransactionReason *reason);

const char *OSMGAModeTransactionReasonString(OSMGAModeTransactionReason reason);

#endif /* OPENSTEP_MGA_MODE_TRANSACTION_H */
