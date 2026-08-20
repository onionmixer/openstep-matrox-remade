/*
 * OpenStepMGAMesaBuffer.c - see the header.
 */

#include <stdio.h>
#include <sys/types.h>
#include <mach/mach.h>

#include "OpenStepMGAMesaBuffer.h"
#include "OpenStepMGAMesaProbe.h"

extern caddr_t mmap(caddr_t, int, int, int, int, long);

#define PROT_READ   0x01
#define PROT_WRITE  0x02
#define MAP_SHARED  0x0001

static unsigned long bufOrigin;
static unsigned long bufWidth, bufHeight, bufStride;
static void *bufMapped;
static unsigned long bufBytes;

unsigned long OSMGAMesaBufferOrigin(void) { return bufOrigin; }
unsigned long OSMGAMesaBufferWidth(void)  { return bufWidth;  }
unsigned long OSMGAMesaBufferHeight(void) { return bufHeight; }
unsigned long OSMGAMesaBufferStride(void) { return bufStride; }

/*
 * Give the surface back.  Called when the context that owns it goes away,
 * and when a fork leaves a child holding a mapping made through a descriptor
 * that is no longer its own.  Without it the first context to be destroyed
 * took the only surface with it and nothing in the process could accelerate
 * again.
 */
void
OpenStepMesaAccelReleaseBuffer(void)
{
    if (bufMapped != 0) {
        (void)vm_deallocate(task_self(), (vm_address_t)bufMapped,
                            (vm_size_t)bufBytes);
        bufMapped = 0;
        bufBytes = 0UL;
    }
    bufOrigin = 0UL;
    bufWidth = bufHeight = bufStride = 0UL;
}

void *
OpenStepMesaAccelBuffer(void *ctx, void *buffer, int width, int height,
                        int *rowLength)
{
    OSMGAMesaProbe probe;
    unsigned long stride, need, avail;
    vm_address_t addr = 0;

    (void)ctx;
    (void)buffer;

    if (width <= 0 || height <= 0 || rowLength == 0)
        return 0;

    /*
     * Binding again at the same size is the ordinary case and gets the same
     * surface back.  Clearing the description first, as this used to, meant a
     * triangle function installed for the previous binding could submit
     * against an origin of zero and a size of zero -- the description has to
     * stay true for as long as anything might still be drawing through it.
     */
    if (bufMapped != 0) {
        if (bufWidth == (unsigned long)width &&
            bufHeight == (unsigned long)height) {
            *rowLength = (int)bufStride;
            return bufMapped;
        }
        /*
         * A different size would need a different surface, and there is only
         * one.  Refused rather than resized: the description is left alone
         * and the caller renders in software, which is wrong only in being
         * slow.
         */
        return 0;
    }

    OSMGAMesaProbeRun(&probe);
    if (probe.verdict != OSMGA_PROBE_HARDWARE)
        return 0;

    stride = probe.caps[OSMGA_HW3D_CAP_STRIDE];
    avail  = probe.caps[OSMGA_HW3D_CAP_VRAMLEN];
    if (stride == 0UL || (unsigned long)width > stride)
        return 0;               /* wider than a row: it would wrap */

    /*
     * Rows times a row of bytes, checked by division so the product is never
     * formed: a height a caller is entitled to ask for can still overflow a
     * thirty-two bit multiply, and a check that overflows is not a check.
     */
    if ((unsigned long)height > avail / (stride * 4UL))
        return 0;
    need = (unsigned long)height * stride * 4UL;

    if (vm_allocate(task_self(), &addr, (vm_size_t)need, TRUE) != KERN_SUCCESS)
        return 0;
    if ((int)mmap((caddr_t)addr, (int)need, PROT_READ | PROT_WRITE,
                  MAP_SHARED, OSMGAMesaProbeDeviceFd(),
                  (long)probe.caps[OSMGA_HW3D_CAP_VRAMOFF]) == -1) {
        (void)vm_deallocate(task_self(), addr, (vm_size_t)need);
        return 0;
    }

    bufMapped = (void *)addr;
    bufBytes  = need;
    bufOrigin = probe.caps[OSMGA_HW3D_CAP_VRAMOFF];
    bufWidth  = (unsigned long)width;
    bufHeight = (unsigned long)height;
    bufStride = stride;

    /*
     * The engine will read this surface at the display's stride, so Mesa has
     * to write it at the same one.  Nothing is flipped: OSMesa puts GL row y
     * at base + y * pitch by default, and the engine draws its rows from the
     * origin the same way, so the two agree without being told to.
     */
    *rowLength = (int)stride;
    return bufMapped;
}
