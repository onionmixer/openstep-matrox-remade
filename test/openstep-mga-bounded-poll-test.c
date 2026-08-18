#include <stdio.h>

#include "OpenStepMGABoundedPoll.h"

static int failures;

static void
expect(int condition, const char *name)
{
    if (!condition) {
        printf("OPENSTEP_MGA_BOUNDED_POLL_TEST=fail:%s\n", name);
        failures++;
    }
}

int
main(void)
{
    OSMGABoundedPollPolicy policy;
    OSMGABoundedPollState state;

    policy.timeout_msec = 25UL;
    policy.required_consecutive_ready = 3;
    expect(OSMGAInitializeBoundedPoll(&policy, &state) == 1, "init");
    expect(OSMGAObserveBoundedPoll(&policy, &state, 0UL, 1) ==
           OSMGA_BOUNDED_POLL_CONTINUE, "first-ready-continues");
    expect(OSMGAObserveBoundedPoll(&policy, &state, 1UL, 0) ==
           OSMGA_BOUNDED_POLL_CONTINUE, "not-ready-resets");
    expect(OSMGAObserveBoundedPoll(&policy, &state, 2UL, 1) ==
           OSMGA_BOUNDED_POLL_CONTINUE, "second-first-ready");
    expect(OSMGAObserveBoundedPoll(&policy, &state, 3UL, 1) ==
           OSMGA_BOUNDED_POLL_CONTINUE, "second-ready-continues");
    expect(OSMGAObserveBoundedPoll(&policy, &state, 4UL, 1) ==
           OSMGA_BOUNDED_POLL_READY, "stable-ready");
    expect(OSMGAObserveBoundedPoll(&policy, &state, 24UL, 0) ==
           OSMGA_BOUNDED_POLL_READY, "ready-is-terminal");

    expect(OSMGAInitializeBoundedPoll(&policy, &state) == 1, "timeout-init");
    expect(OSMGAObserveBoundedPoll(&policy, &state, 24UL, 1) ==
           OSMGA_BOUNDED_POLL_CONTINUE, "before-deadline");
    expect(OSMGAObserveBoundedPoll(&policy, &state, 25UL, 1) ==
           OSMGA_BOUNDED_POLL_TIMEOUT, "deadline-timeout");
    expect(OSMGAObserveBoundedPoll(&policy, &state, 0UL, 1) ==
           OSMGA_BOUNDED_POLL_TIMEOUT, "timeout-is-terminal");

    policy.timeout_msec = 0;
    expect(OSMGAInitializeBoundedPoll(&policy, &state) == 0,
           "zero-timeout-rejected");
    expect(OSMGAObserveBoundedPoll(&policy, &state, 0UL, 0) ==
           OSMGA_BOUNDED_POLL_INVALID_ARGUMENT, "zero-timeout-reason");
    expect(OSMGABoundedPollResultString(OSMGA_BOUNDED_POLL_READY)[0] == 'r',
           "result-string");

    if (failures != 0) {
        return 1;
    }
    printf("OPENSTEP_MGA_BOUNDED_POLL_TEST_STATUS=pass\n");
    return 0;
}
