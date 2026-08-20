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

    /* The same right triangle the Objective-C test draws, so the two can be
     * compared directly: 400 pixels, and the colours of its corners. */
    v0.x =  0L; v0.y =  0L; v0.r = 255UL; v0.g =   0UL; v0.b =   0UL;
    v1.x =  0L; v1.y = 20L; v1.r =   0UL; v1.g = 255UL; v1.b =   0UL;
    v2.x = 40L; v2.y = 20L; v2.r =   0UL; v2.g =   0UL; v2.b = 255UL;

    n = OSMGAMesaBuildTriangle(&v0, &v1, &v2, (OSMGAMesaVertex *)0,
                               batch->tri);
    batch->triCount = (unsigned long)n;

    rc = OSMGAMesaProbeSubmit(&res);
    printf("   submit rc=%d verdict=%lu triangle=%lu dwords=%lu spins=%lu\n",
           rc, res.verdict, res.triangle, res.dwords, res.spins);
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

    /* A refusal must come back as a refusal, not as a silent success. */
    batch->triCount = OSMGA_HW3D_MAX_TRI + 1UL;
    rc = OSMGAMesaProbeSubmit(&res);
    printf("   too many triangles -> rc=%d verdict=%lu %s\n",
           rc, res.verdict, (rc != 0) ? "ok" : "FAIL");
    if (rc == 0)
        failures++;

    printf("%s\n", failures ? "FAIL"
                            : "PASS -- drawn by hardware, with no Objective-C "
                              "anywhere in this program");
    return failures ? 1 : 0;
}
