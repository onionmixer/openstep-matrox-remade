/* Pure-C bounded readiness/stability policy; no target interfaces. */

#include "OpenStepMGABoundedPoll.h"

int
OSMGAInitializeBoundedPoll(const OSMGABoundedPollPolicy *policy,
                           OSMGABoundedPollState *state)
{
    if (policy == 0 || state == 0 || policy->timeout_msec == 0 ||
        policy->required_consecutive_ready == 0) {
        return 0;
    }
    state->consecutive_ready = 0;
    state->terminal_result = OSMGA_BOUNDED_POLL_CONTINUE;
    return 1;
}

OSMGABoundedPollResult
OSMGAObserveBoundedPoll(const OSMGABoundedPollPolicy *policy,
                        OSMGABoundedPollState *state,
                        unsigned long elapsed_msec, int ready)
{
    if (policy == 0 || state == 0 || policy->timeout_msec == 0 ||
        policy->required_consecutive_ready == 0) {
        return OSMGA_BOUNDED_POLL_INVALID_ARGUMENT;
    }
    if (state->terminal_result != OSMGA_BOUNDED_POLL_CONTINUE) {
        return state->terminal_result;
    }
    if (elapsed_msec >= policy->timeout_msec) {
        state->terminal_result = OSMGA_BOUNDED_POLL_TIMEOUT;
        return state->terminal_result;
    }
    if (!ready) {
        state->consecutive_ready = 0;
        return OSMGA_BOUNDED_POLL_CONTINUE;
    }
    state->consecutive_ready++;
    if (state->consecutive_ready < policy->required_consecutive_ready) {
        return OSMGA_BOUNDED_POLL_CONTINUE;
    }
    state->terminal_result = OSMGA_BOUNDED_POLL_READY;
    return state->terminal_result;
}

const char *
OSMGABoundedPollResultString(OSMGABoundedPollResult result)
{
    switch (result) {
    case OSMGA_BOUNDED_POLL_CONTINUE:
        return "continue";
    case OSMGA_BOUNDED_POLL_READY:
        return "ready";
    case OSMGA_BOUNDED_POLL_TIMEOUT:
        return "timeout";
    case OSMGA_BOUNDED_POLL_INVALID_ARGUMENT:
        return "invalid-argument";
    }
    return "unknown";
}
