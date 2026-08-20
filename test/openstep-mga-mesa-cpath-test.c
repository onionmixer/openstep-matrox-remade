/*
 * openstep-mga-mesa-cpath-test.c -- M1-3b-3: a hardware triangle, from C.
 *
 * Every earlier client reached the driver through IODeviceMaster, which is
 * Objective-C.  libGL cannot link that, so none of them proved the path the
 * library will actually take.  This one does: it probes, maps, builds and
 * submits without a single Objective-C symbol, and the absence of -lDriver
 * on its build line is the point of the exercise.
 *
 *   cc -O -Wall -o /tmp/cpath openstep-mga-mesa-cpath-test.c \
 *      ../mesa/OpenStepMGAMesaProbe.c ../mesa/OpenStepMGAMesaTriangle.c
 */

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <mach/mach.h>

#include "../mesa/OpenStepMGAMesaProbe.h"
#include "../mesa/OpenStepMGAMesaTriangle.h"

extern caddr_t mmap(caddr_t, int, int, int, int, long);

#define PROT_READ   0x01
#define PROT_WRITE  0x02
#define MAP_SHARED  0x0001

#define VRAM_BLOCK  (4UL * 1024UL * 1024UL)
#define STRIDE_DW   1024UL
#define ROWS        40UL
#define COLS        64UL
#define SENTINEL    0x5A5A5A5AUL

static int failures;

static caddr_t
mapWindow(int fd, unsigned long offset, int len)
{
    vm_address_t addr = 0;

    if (vm_allocate(task_self(), &addr, (vm_size_t)len, TRUE) != KERN_SUCCESS)
        return (caddr_t)-1;
    if ((int)mmap((caddr_t)addr, len, PROT_READ | PROT_WRITE, MAP_SHARED,
                  fd, (long)offset) == -1)
        return (caddr_t)-1;
    return (caddr_t)addr;
}

int
main(void)
{
    OSMGAMesaProbe probe;
    OSMGAHW3DBatch *batch;
    OSMGAHW3DSubmitBlock res;
    OSMGAMesaVertex v0, v1, v2;
    volatile unsigned long *vram;
    caddr_t win;
    unsigned long row, col, drawn;
    int n, rc;

    OSMGAMesaProbeRun(&probe);
    printf("M1-3b-3: the whole path in C -- %s\n",
           OSMGAMesaProbeVerdictString(probe.verdict));
    if (probe.verdict != OSMGA_PROBE_HARDWARE) {
        printf("   nothing to test: software is the correct answer here\n");
        return 0;
    }

    if ((batch = OSMGAMesaProbeBatch()) == 0) {
        printf("   FAIL -- the command window would not map\n");
        return 1;
    }
    win = mapWindow(OSMGAMesaProbeDeviceFd(), VRAM_BLOCK,
                    (int)(ROWS * STRIDE_DW * 4UL));
    if (win == (caddr_t)-1) {
        printf("   FAIL -- the VRAM window would not map\n");
        return 1;
    }
    vram = (volatile unsigned long *)win;

    for (row = 0UL; row < ROWS; row++)
        for (col = 0UL; col < COLS; col++)
            vram[row * STRIDE_DW + col] = SENTINEL;

    memset(batch, 0, sizeof *batch);
    batch->magic = OSMGA_HW3D_MAGIC;
    batch->version = OSMGA_HW3D_VERSION;
    batch->state.dstorg = VRAM_BLOCK;
    /* The batch declares what it may touch; the kernel proves that lies
     * inside the window it owns and clips to it. */
    batch->state.dstWidth  = 64UL;
    batch->state.dstHeight = 120UL;
    batch->state.dstPitch  = 1024UL;   /* the display stride, as before */

    /* The same right triangle the Objective-C test draws, so the two can be
     * compared directly: 400 pixels, and the colours of its corners. */
    v0.x =  0L; v0.y =  0L; v0.r = 255UL; v0.g =   0UL; v0.b =   0UL;
    v1.x =  0L; v1.y = 20L; v1.r =   0UL; v1.g = 255UL; v1.b =   0UL;
    v2.x = 40L; v2.y = 20L; v2.r =   0UL; v2.g =   0UL; v2.b = 255UL;

    n = OSMGAMesaBuildTriangle(&v0, &v1, &v2, (OSMGAMesaVertex *)0,
                               OSMGA_MESA_ZMODE_NONE, batch->tri);
    batch->triCount = (unsigned long)n;

    rc = OSMGAMesaProbeSubmit(&res);
    printf("   submit rc=%d status=%lu verdict=%lu triangle=%lu dwords=%lu "
           "spins=%lu\n", rc, res.status, res.verdict, res.triangle,
           res.dwords, res.spins);
    if (rc != 0) {
        printf("   FAIL -- the batch was refused\n");
        return 1;
    }

    drawn = 0UL;
    for (row = 0UL; row < ROWS; row++)
        for (col = 0UL; col < COLS; col++)
            if (vram[row * STRIDE_DW + col] != SENTINEL)
                drawn++;
    printf("   drew %lu pixels, wanted 400\n", drawn);
    if (drawn != 400UL)
        failures++;

    {
        static const struct { unsigned long x, y, r, g, b; } want[3] = {
            {  0UL,  0UL, 255UL,   0UL,   0UL },
            {  0UL, 10UL, 127UL, 127UL,   0UL },
            { 20UL, 10UL, 127UL,   0UL, 127UL }
        };
        int i;

        for (i = 0; i < 3; i++) {
            unsigned long px = vram[want[i].y * STRIDE_DW + want[i].x];
            unsigned long gr = (px >> 16) & 0xffUL;
            unsigned long gg = (px >>  8) & 0xffUL;
            unsigned long gb =  px        & 0xffUL;
            int ok = (gr == want[i].r && gg == want[i].g && gb == want[i].b);

            printf("     (%2lu,%2lu) got %3lu %3lu %3lu  %s\n",
                   want[i].x, want[i].y, gr, gg, gb, ok ? "ok" : "FAIL");
            if (!ok)
                failures++;
        }
    }

    /*
     * A refusal must come back as a refusal AND say why.  Checking only that
     * it failed is what let the first version through: the ioctl returned
     * EINVAL, which meant the block was never copied back, so the verdict
     * read zero -- the code for "drew fine" -- and the test was satisfied.
     */
    batch->triCount = OSMGA_HW3D_MAX_TRI + 1UL;
    rc = OSMGAMesaProbeSubmit(&res);
    printf("   too many triangles -> rc=%d verdict=%lu (want %d) %s\n",
           rc, res.verdict, OSMGA_HW3D_E_COUNT,
           (rc != 0 && res.verdict == (unsigned long)OSMGA_HW3D_E_COUNT)
               ? "ok" : "FAIL");
    if (rc == 0 || res.verdict != (unsigned long)OSMGA_HW3D_E_COUNT)
        failures++;

    /*
     * The rules the kernel gained with version 2, each checked on the
     * hardware rather than only in the host suite -- the host suite calls
     * the validator directly, so it cannot show that the driver actually
     * consults it before touching the engine.
     */
    {
        static const struct {
            const char *what;
            unsigned long w, h, org;
            long ar6;
            unsigned long want;
        } refuse[8] = {
            { "no width",              0UL,  120UL, VRAM_BLOCK, -1L,
              OSMGA_HW3D_E_DSTSIZE },
            { "no height",            64UL,    0UL, VRAM_BLOCK, -1L,
              OSMGA_HW3D_E_DSTSIZE },
            /* Wider than the pitch is a pitch fault now, not a size one:
             * the pitch is what a row has to fit inside, and it is checked
             * before anything is measured against it. */
            { "wider than the pitch",  1025UL, 8UL, VRAM_BLOCK, -1L,
              OSMGA_HW3D_E_DSTPITCH },
            { "taller than the window", 64UL, 4096UL, VRAM_BLOCK, -1L,
              OSMGA_HW3D_E_DSTSIZE },
            { "a zero edge divisor",   64UL,  120UL, VRAM_BLOCK,  0L,
              OSMGA_HW3D_E_EDGEDIV },
            { "no pitch",              64UL,  120UL, VRAM_BLOCK, -1L,
              OSMGA_HW3D_E_DSTPITCH },
            { "a pitch past the register", 64UL, 120UL, VRAM_BLOCK, -1L,
              OSMGA_HW3D_E_DSTPITCH },
            { "a row wider than its pitch", 64UL, 120UL, VRAM_BLOCK, -1L,
              OSMGA_HW3D_E_DSTPITCH }
        };
        int i;

        for (i = 0; i < 8; i++) {
            batch->triCount = 1UL;
            batch->state.dstorg    = refuse[i].org;
            batch->state.dstWidth  = refuse[i].w;
            batch->state.dstHeight = refuse[i].h;
            batch->state.dstPitch  = 1024UL;
            if (i == 5) batch->state.dstPitch = 0UL;
            if (i == 6) batch->state.dstPitch = 4096UL;
            if (i == 7) batch->state.dstWidth = 2048UL;
            if (refuse[i].ar6 >= 0L)
                batch->tri[0].ar6 = refuse[i].ar6;
            rc = OSMGAMesaProbeSubmit(&res);
            printf("   %-24s -> rc=%d verdict=%lu (want %lu) %s\n",
                   refuse[i].what, rc, res.verdict, refuse[i].want,
                   (rc != 0 && res.verdict == refuse[i].want) ? "ok" : "FAIL");
            if (rc == 0 || res.verdict != refuse[i].want)
                failures++;
            /* put the good geometry back for the next case */
            n = OSMGAMesaBuildTriangle(&v0, &v1, &v2,
                                       (OSMGAMesaVertex *)0, batch->tri);
        }
    }

    /*
     * The kernel builds its command list in the same allocation, past the
     * batch.  A client that could map that far could rewrite the list after
     * it had been validated and before the engine read it, which is the one
     * thing the validate-then-encode split exists to prevent.  Asking for
     * the whole allocation must fail.
     */
    {
        caddr_t whole = mapWindow(OSMGAMesaProbeDeviceFd(),
                                  0x40000000UL, 64 * 1024);

        printf("   mapping past the batch -> %s\n",
               (whole == (caddr_t)-1) ? "refused, ok"
                                      : "GRANTED -- FAIL");
        if (whole != (caddr_t)-1)
            failures++;
    }

    printf("%s\n", failures ? "FAIL"
                            : "PASS -- drawn by hardware, with no Objective-C "
                              "anywhere in this program");
    return failures ? 1 : 0;
}
