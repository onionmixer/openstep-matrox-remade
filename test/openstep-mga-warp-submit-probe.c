/*
 * openstep-mga-warp-submit-probe.c -- does the kernel execute a version 10
 * WARP batch?
 *
 * FIVE TESTS IN ORDER, because one live draw is not an interpretable
 * reboot -- a failure would not say whether the dispatch, the validation,
 * the encoding or the engine was at fault:
 *
 *   1  version 9 control BEFORE      the old path still answers
 *   2  version 10 DRY, valid         dispatch, snapshot, validation and
 *                                    encoding, without touching the engine
 *   3  version 10 DRY, malformed     a refusal rings no doorbell
 *   4  version 10 LIVE               the 1176 pixel triangle, read back
 *   5  version 9 control AFTER       the final stop left version 9 usable
 *
 * The dry command exists for exactly this: it validates and encodes and
 * then stops before the first register write.
 *
 * There is deliberately NO timeout test here.  Its outcome latches
 * acceleration off permanently, and that costs another reboot.
 */
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <mach/mach.h>
#include "OpenStepMGAMesaProbe.h"
#include "OpenStepMGAHW3D.h"

static int failures;

static void
say(const char *what, int rc, const OSMGAHW3DSubmitBlock *r)
{
    printf("  %-28s rc %3d  status %lu verdict %lu dwords %lu\n",
           what, rc, r->status, r->verdict, r->dwords);
}

static void
expect(const char *what, int rc, const OSMGAHW3DSubmitBlock *r,
       unsigned long wantVerdict)
{
    say(what, rc, r);
    if (r->verdict != wantVerdict) {
        printf("  FAIL: %s wanted verdict %lu\n", what, wantVerdict);
        failures++;
    }
}

/*
 * A version 9 batch that draws nothing.  Legal, validates, and reaches the
 * whole dispatch: it proves the old path still answers without depending
 * on the trapezoid builder.
 */
static void
v9control(const char *when, unsigned long dstorg, unsigned long pitch)
{
    OSMGAHW3DBatch *b = OSMGAMesaProbeBatch();
    OSMGAHW3DSubmitBlock r;
    int rc;

    if (b == 0) { printf("  no batch mapping\n"); failures++; return; }
    /*
     * The STATE too, not just the header.  The first run of this probe set
     * only magic, version and the count, and the control failed with
     * E_DSTPITCH -- against whatever the mapped buffer happened to hold.
     * The control after it passed only because the version 10 fixture had
     * filled the state by then, which is the same fixture bug wearing a
     * pass.
     */
    memset(b, 0, sizeof *b);
    b->magic    = OSMGA_HW3D_MAGIC;
    b->version  = OSMGA_HW3D_VERSION;
    b->triCount = 0UL;
    b->state.dstorg    = dstorg;
    b->state.dstWidth  = 64UL;
    b->state.dstHeight = 64UL;
    b->state.dstPitch  = pitch;
    b->state.zorg      = dstorg;
    b->state.texorg    = dstorg;
    memset(&r, 0, sizeof r);
    rc = OSMGAMesaProbeSubmit(&r);
    expect(when, rc, &r, (unsigned long)OSMGA_HW3D_OK);
}

/*
 * One WARP triangle: the 48-leg right triangle the qualification harness
 * drew, 1176 pixels, so the expected picture is one the hardware has
 * already agreed to twice.
 */
static unsigned int
f32(double d)
{
    float f = (float)d;
    unsigned int u;

    memcpy(&u, &f, sizeof u);
    return u;
}

static void
fillWarp(OSMGAHW3DWarpBatch *w, unsigned long dstorg, unsigned long pitch,
         unsigned long width, unsigned long height)
{
    static const double xs[3] = {  8.0, 56.0,  8.0 };
    static const double ys[3] = {  8.0,  8.0, 56.0 };
    unsigned long i;

    memset(w, 0, sizeof *w);
    w->magic    = OSMGA_HW3D_MAGIC;
    w->version  = OSMGA_HW3D_VERSION_WARP;
    w->triCount = 0UL;
    w->runCount = 1U;
    w->vtxCount = 3U;

    w->state.dstorg    = dstorg;
    w->state.dstWidth  = width;
    w->state.dstHeight = height;
    w->state.dstPitch  = pitch;
    w->state.zorg      = dstorg;      /* unused: no run addresses depth */
    w->state.texorg    = dstorg;
    w->state.texW      = 0UL;
    w->state.texH      = 0UL;
    w->state.texPitch  = 0UL;
    w->state.texFormat = 0UL;
    w->state.scissorOn = 0UL;

    /* TRAP with atype I: colour only, no depth addressed. */
    w->run[0].dwgctl    = 0x000c4074U;
    w->run[0].alphactrl = 0x00000101U;
    w->run[0].first     = 0U;
    w->run[0].count     = 3U;

    for (i = 0UL; i < 3UL; i++) {
        w->vtx[i].x        = f32(xs[i]);
        w->vtx[i].y        = f32(ys[i]);
        w->vtx[i].z        = f32(0.5);
        w->vtx[i].rhw      = f32(1.0);
        w->vtx[i].diffuse  = 0xFFFF8040U;
        w->vtx[i].specular = 0U;
        w->vtx[i].tu0      = 0U;
        w->vtx[i].tv0      = 0U;
    }
}

/*
 * DID IT DRAW?
 *
 * The verdict says the submission completed; it does not say a pixel
 * moved.  So the destination is mapped, painted with a sentinel, drawn
 * into, and counted against the shape the qualification harness has
 * agreed with the trapezoid path twice: a right triangle with legs of 48
 * at (8,8), 1176 pixels, and nothing outside it touched.
 */
#define SENTINEL   0x5A5A5AUL
#define TRI_LO     8UL
#define TRI_LEG    48UL

static unsigned long *surface;
static unsigned long surfaceStride;

static int
mapSurface(unsigned long vramoff, unsigned long stride)
{
    vm_address_t addr = 0;
    unsigned long need = 64UL * stride * 4UL;

    if (vm_allocate(task_self(), &addr, (vm_size_t)need, TRUE) !=
        KERN_SUCCESS)
        return 0;
    if ((int)mmap((caddr_t)addr, (int)need, PROT_READ | PROT_WRITE,
                  MAP_SHARED, OSMGAMesaProbeDeviceFd(), (long)vramoff)
        == -1) {
        (void)vm_deallocate(task_self(), addr, (vm_size_t)need);
        return 0;
    }
    surface       = (unsigned long *)addr;
    surfaceStride = stride;
    return 1;
}

static void
paint(unsigned long v)
{
    unsigned long row, col;

    for (row = 0UL; row < 64UL; row++)
        for (col = 0UL; col < 64UL; col++)
            surface[row * surfaceStride + col] = v;
}

static void
checkShape(void)
{
    unsigned long row, col, drawn = 0UL, wrong = 0UL, outside = 0UL;

    for (row = 0UL; row < 64UL; row++)
        for (col = 0UL; col < 64UL; col++) {
            unsigned long v = surface[row * surfaceStride + col] & 0xFFFFFFUL;
            int inside = (row >= TRI_LO && row < TRI_LO + TRI_LEG &&
                          col >= TRI_LO && col < TRI_LO + TRI_LEG &&
                          (row - TRI_LO) + (col - TRI_LO) <= TRI_LEG - 1UL);

            if (inside) {
                if (v == (0xFFFF8040UL & 0xFFFFFFUL)) drawn++;
                else wrong++;
            } else if (v != SENTINEL) {
                outside++;
            }
        }
    printf("  drawn %lu of %lu, wrong %lu, outside touched %lu\n",
           drawn, TRI_LEG * (TRI_LEG + 1UL) / 2UL, wrong, outside);
    if (drawn != TRI_LEG * (TRI_LEG + 1UL) / 2UL || wrong != 0UL ||
        outside != 0UL) {
        printf("  FAIL: the picture is not the 1176 pixel triangle\n");
        failures++;
    }
}

int
main(void)
{
    OSMGAMesaProbe p;
    OSMGAHW3DWarpBatch *w;
    OSMGAHW3DSubmitBlock r;
    unsigned long dstorg, pitch;
    int rc;

    OSMGAMesaProbeRun(&p);
    printf("probe verdict %d (%s)\n", (int)p.verdict,
           OSMGAMesaProbeVerdictString(p.verdict));
    if (p.verdict != OSMGA_PROBE_HARDWARE)
        return 1;

    /*
     * The stride the engine actually walks, and an offscreen origin inside
     * the window the driver published -- both from the capabilities rather
     * than assumed, because the validator compares the batch's pitch
     * against the kernel's display pitch and a guess would be refused.
     */
    pitch  = p.caps[OSMGA_HW3D_CAP_STRIDE];
    dstorg = p.caps[OSMGA_HW3D_CAP_VRAMOFF];
    printf("stride %lu, window %lu..%lu\n", pitch, dstorg,
           dstorg + p.caps[OSMGA_HW3D_CAP_VRAMLEN]);

    printf("1. version 9 control, before\n");
    v9control("v9 empty batch", dstorg, pitch);

    w = (OSMGAHW3DWarpBatch *)OSMGAMesaProbeBatch();
    if (w == 0) { printf("no batch mapping\n"); return 1; }

    printf("2. version 10 dry, a batch that should pass\n");
    fillWarp(w, dstorg, pitch, 64UL, 64UL);
    memset(&r, 0, sizeof r);
    rc = OSMGAMesaProbeSubmitDry(&r);
    expect("v10 dry valid", rc, &r, (unsigned long)OSMGA_HW3D_OK);
    if (r.dwords == 0UL) {
        printf("  FAIL: a valid batch encoded nothing\n");
        failures++;
    }

    printf("3. version 10 dry, batches that should not\n");
    fillWarp(w, dstorg, pitch, 64UL, 64UL);
    w->vtxCount = 4U;                       /* not whole triangles */
    memset(&r, 0, sizeof r);
    rc = OSMGAMesaProbeSubmitDry(&r);
    expect("v10 dry, vertex count", rc, &r,
           (unsigned long)OSMGA_HW3D_E_VTXCOUNT);

    fillWarp(w, dstorg, pitch, 64UL, 64UL);
    w->vtx[1].rhw = 0U;                     /* zero weight */
    memset(&r, 0, sizeof r);
    rc = OSMGAMesaProbeSubmitDry(&r);
    expect("v10 dry, zero rhw", rc, &r, (unsigned long)OSMGA_HW3D_E_VTXFLOAT);

    fillWarp(w, dstorg, pitch, 64UL, 64UL);
    w->run[0].dwgctl = 0x000c4075U;         /* opcode the engine lacks */
    memset(&r, 0, sizeof r);
    rc = OSMGAMesaProbeSubmitDry(&r);
    expect("v10 dry, bad opcode", rc, &r, (unsigned long)OSMGA_HW3D_E_DWGCTL);

    fillWarp(w, dstorg, pitch, 64UL, 64UL);
    w->state.dstorg = 0UL;                  /* outside the window */
    memset(&r, 0, sizeof r);
    rc = OSMGAMesaProbeSubmitDry(&r);
    expect("v10 dry, origin outside", rc, &r,
           (unsigned long)OSMGA_HW3D_E_DSTORG);

    printf("4. version 10 live\n");
    if (!mapSurface(dstorg, pitch)) {
        printf("  FAIL: could not map the destination for readback\n");
        failures++;
    } else {
        paint(SENTINEL);
    }
    fillWarp(w, dstorg, pitch, 64UL, 64UL);
    memset(&r, 0, sizeof r);
    rc = OSMGAMesaProbeSubmit(&r);
    expect("v10 live", rc, &r, (unsigned long)OSMGA_HW3D_OK);
    if (surface != 0)
        checkShape();

    printf("5. version 9 control, after\n");
    v9control("v9 empty batch", dstorg, pitch);

    printf("%s: %d failing\n",
           (failures == 0) ? "warp-submit PASS" : "warp-submit", failures);
    return failures != 0;
}
