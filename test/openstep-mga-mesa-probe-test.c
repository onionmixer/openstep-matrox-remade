/*
 * openstep-mga-mesa-probe-test.c - M1-3a: exercise the libGL-side decision.
 *
 * Built exactly as an application would be -- no -lDriver, no Objective-C --
 * because that is half of what is being tested.
 *
 * Run it twice: once plainly, and once with OSMGA_MESA_ACCEL=0, which is the
 * only way the software path gets exercised on a machine that has the card.
 */

#include <stdio.h>
#include <string.h>
#include "../mesa/OpenStepMGAMesaProbe.h"

static int failures;

static void
expect(const char *what, int ok)
{
    printf("  %-34s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok)
        failures++;
}

int
main(int argc, char **argv)
{
    OSMGAMesaProbe p, q, r;
    unsigned i;
    int revokeFirst = (argc > 1 && strcmp(argv[1], "revoke-first") == 0);

    if (revokeFirst) {
        /*
         * Revoking before anything has probed used to leave the cached
         * descriptor at zero, and a caller that tidied up "its" descriptor
         * would then close the application's standard input.
         */
        printf("revoke before any probe:\n");
        OSMGAMesaProbeRevoke("nothing has probed yet");
        OSMGAMesaProbeRun(&p);
        expect("verdict is not hardware", p.verdict != OSMGA_PROBE_HARDWARE);
        expect("no descriptor is lent out",
               OSMGAMesaProbeDeviceFd() < 0);
        return failures ? 1 : 0;
    }

    OSMGAMesaProbeRun(&p);
    printf("verdict: %s\n", OSMGAMesaProbeVerdictString(p.verdict));
    printf("fd:      %d\n", OSMGAMesaProbeDeviceFd());
    if (p.verdict == OSMGA_PROBE_UNAVAILABLE)
        printf("missing: %08lx\n", p.missing);
    if (p.caps[OSMGA_HW3D_CAP_MAGIC] != 0UL) {
        printf("caps:   ");
        for (i = 0U; i < OSMGA_HW3D_CAPS_COUNT; i++)
            printf(" %lu", p.caps[i]);
        printf("\n");
    }

    OSMGAMesaProbeRun(&q);
    expect("the verdict is stable", q.verdict == p.verdict);
    expect("a lent descriptor is never 0, 1 or 2",
           OSMGAMesaProbeDeviceFd() < 0 || OSMGAMesaProbeDeviceFd() > 2);
    expect("hardware lends a descriptor, software does not",
           (p.verdict == OSMGA_PROBE_HARDWARE) ==
           (OSMGAMesaProbeDeviceFd() >= 0));

    OSMGAMesaProbeRevoke("test");
    OSMGAMesaProbeRun(&r);
    printf("after revoke: %s\n", OSMGAMesaProbeVerdictString(r.verdict));
    expect("revocation sticks", r.verdict != OSMGA_PROBE_HARDWARE);
    expect("revocation takes the descriptor back",
           OSMGAMesaProbeDeviceFd() < 0);

    OSMGAMesaProbeRevoke("again");
    expect("revoking twice is harmless", OSMGAMesaProbeDeviceFd() < 0);

    printf("%d failing\n", failures);
    return failures ? 1 : 0;
}
