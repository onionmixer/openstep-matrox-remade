/*
 * Do SUBMIT and SUBMIT_DRY answer the same way for the SAME valid batch?
 *
 * Written because arm B revoked acceleration on its first frame and neither
 * the driver log nor the hook's refusal record said why.  This uses the
 * library's own mapping so the batch is a real one, and asks both commands
 * about it in turn.
 */
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include "OpenStepMGAMesaProbe.h"
#include "OpenStepMGAHW3D.h"

int main(void)
{
    OSMGAMesaProbe p;
    OSMGAHW3DBatch *b;
    OSMGAHW3DSubmitBlock r;
    int rc;

    OSMGAMesaProbeRun(&p);
    printf("probe verdict %d (%s)\n", (int)p.verdict,
           OSMGAMesaProbeVerdictString(p.verdict));
    if (p.verdict != OSMGA_PROBE_HARDWARE) return 1;

    b = OSMGAMesaProbeBatch();
    if (b == 0) { printf("no batch mapping\n"); return 1; }

    /* An EMPTY batch: legal, validates, draws nothing.  Enough to tell the
     * two commands apart without depending on the triangle builder. */
    b->triCount = 0UL;

    memset(&r, 0, sizeof r);
    rc = OSMGAMesaProbeSubmit(&r);
    printf("SUBMIT      rc %3d  status %lu verdict %lu dwords %lu\n",
           rc, r.status, r.verdict, r.dwords);

    b->triCount = 0UL;
    memset(&r, 0, sizeof r);
    rc = OSMGAMesaProbeSubmitDry(&r);
    printf("SUBMIT_DRY  rc %3d  status %lu verdict %lu dwords %lu\n",
           rc, r.status, r.verdict, r.dwords);
    return 0;
}
