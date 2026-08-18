/*
 * OpenStepMGABoundedPoll.h - pure-C bounded readiness/stability policy.
 *
 * Callers provide elapsed time and a sampled readiness bit.  This module does
 * not read a device register, pause execution, or obtain a clock itself.
 */

#ifndef OPENSTEP_MGA_BOUNDED_POLL_H
#define OPENSTEP_MGA_BOUNDED_POLL_H

typedef struct {
    unsigned long timeout_msec;
    unsigned int required_consecutive_ready;
} OSMGABoundedPollPolicy;

typedef enum {
    OSMGA_BOUNDED_POLL_CONTINUE = 0,
    OSMGA_BOUNDED_POLL_READY,
    OSMGA_BOUNDED_POLL_TIMEOUT,
    OSMGA_BOUNDED_POLL_INVALID_ARGUMENT
} OSMGABoundedPollResult;

typedef struct {
    unsigned int consecutive_ready;
    OSMGABoundedPollResult terminal_result;
} OSMGABoundedPollState;

int OSMGAInitializeBoundedPoll(const OSMGABoundedPollPolicy *policy,
                               OSMGABoundedPollState *state);

/*
 * `elapsed_msec` is measured by the caller from one operation start point.
 * A ready observation at or after the deadline cannot turn a timeout into a
 * success.  Once READY or TIMEOUT is returned, later calls retain completion.
 */
OSMGABoundedPollResult OSMGAObserveBoundedPoll(
    const OSMGABoundedPollPolicy *policy, OSMGABoundedPollState *state,
    unsigned long elapsed_msec, int ready);

const char *OSMGABoundedPollResultString(OSMGABoundedPollResult result);

#endif /* OPENSTEP_MGA_BOUNDED_POLL_H */
