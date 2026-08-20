/*
 * openstep-mga-mesa-probe-test.c - M1-3a: exercise the libGL-side decision.
 *
 * Built exactly as an application would be -- no -lDriver, no Objective-C --
 * because that is half of what is being tested.
 */

#include <stdio.h>
#include "../mesa/OpenStepMGAMesaProbe.h"

int
main(void)
{
    OSMGAMesaProbe p;
    unsigned i;

    OSMGAMesaProbeRun(&p);
    printf("verdict: %s\n", OSMGAMesaProbeVerdictString(p.verdict));
    printf("fd:      %d\n", p.fd);
    if (p.verdict == OSMGA_PROBE_UNAVAILABLE)
        printf("missing: %08lx\n", p.missing);
    if (p.caps[OSMGA_HW3D_CAP_MAGIC] != 0UL) {
        printf("caps:   ");
        for (i = 0U; i < OSMGA_HW3D_CAPS_COUNT; i++)
            printf(" %lu", p.caps[i]);
        printf("\n");
    }

    /* The answer must not change between calls: a decision that could flip
     * mid-process is one the fallback discipline cannot rely on. */
    {
        OSMGAMesaProbe q;

        OSMGAMesaProbeRun(&q);
        printf("stable:  %s\n", (q.verdict == p.verdict) ? "yes" : "NO -- BUG");
    }

    /* Revocation is one-way. */
    OSMGAMesaProbeRevoke("test");
    {
        OSMGAMesaProbe r;

        OSMGAMesaProbeRun(&r);
        printf("after revoke: %s\n", OSMGAMesaProbeVerdictString(r.verdict));
        printf("sticky:  %s\n",
               (r.verdict != OSMGA_PROBE_HARDWARE) ? "yes" : "NO -- BUG");
    }
    return 0;
}
