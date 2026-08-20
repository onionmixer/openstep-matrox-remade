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
static void *bufCtx;      /* the context the surface belongs to */
static void *bufApp;      /* what the application gave us */
static unsigned long bufAppRow;  /* and how its rows are laid out */
static int   bufDirty;
static void *depthMapped;
static unsigned long depthOrigin, depthBytes;
static unsigned long bufBytes;

unsigned long OSMGAMesaBufferOrigin(void) { return bufOrigin; }
unsigned long OSMGAMesaBufferWidth(void)  { return bufWidth;  }
unsigned long OSMGAMesaBufferHeight(void) { return bufHeight; }
unsigned long OSMGAMesaBufferStride(void) { return bufStride; }
unsigned long OSMGAMesaBufferDepthOrigin(void) { return depthOrigin; }

void *
OpenStepMesaAccelDepthBuffer(void *ctx, int width, int height,
                             int bytesPerValue)
{
    OSMGAMesaProbe probe;
    unsigned long need, tail, avail;
    vm_address_t addr = 0;

    if (depthMapped != 0)
        return depthMapped;
    /*
     * Only alongside a colour surface of ours, and only at sixteen bits:
     * that is what the engine writes, and a depth buffer the software path
     * reads at a different width than the engine writes it would be worse
     * than no sharing at all.
     */
    if (bufMapped == 0 || bufCtx != ctx || bytesPerValue != 2)
        return 0;
    /*
     * Only when the surface's pitch IS its width.  The engine reads depth at
     * the pitch the batch declares, and Mesa addresses depth rows by the
     * framebuffer's width and nothing else -- so a caller that asked for a
     * longer row through OSMesaPixelStore would leave the two counting rows
     * differently from the second row onwards, sharing a buffer while
     * disagreeing about where everything in it is.  Colour survives that,
     * because there the row length is honoured on both sides; depth does not.
     */
    if (bufStride != bufWidth)
        return 0;
    if ((unsigned long)width != bufWidth ||
        (unsigned long)height != bufHeight)
        return 0;

    OSMGAMesaProbeRun(&probe);
    if (probe.verdict != OSMGA_PROBE_HARDWARE)
        return 0;

    /*
     * Immediately after the colour surface, page-aligned so it can be mapped
     * on its own.  Checked by division rather than by forming the product,
     * as everywhere else here.
     */
    tail = bufOrigin - probe.caps[OSMGA_HW3D_CAP_VRAMOFF] +
           bufHeight * bufStride * 4UL;
    tail = (tail + 4095UL) & ~4095UL;
    avail = probe.caps[OSMGA_HW3D_CAP_VRAMLEN];
    if (tail >= avail)
        return 0;
    if (bufHeight > (avail - tail) / (bufStride * 2UL))
        return 0;
    need = bufHeight * bufStride * 2UL;
    need = (need + 4095UL) & ~4095UL;

    if (vm_allocate(task_self(), &addr, (vm_size_t)need, TRUE) != KERN_SUCCESS)
        return 0;
    if ((int)mmap((caddr_t)addr, (int)need, PROT_READ | PROT_WRITE,
                  MAP_SHARED, OSMGAMesaProbeDeviceFd(),
                  (long)(probe.caps[OSMGA_HW3D_CAP_VRAMOFF] + tail)) == -1) {
        (void)vm_deallocate(task_self(), addr, (vm_size_t)need);
        return 0;
    }
    depthMapped = (void *)addr;
    depthBytes  = need;
    depthOrigin = probe.caps[OSMGA_HW3D_CAP_VRAMOFF] + tail;
    return depthMapped;
}

/*
 * Give the surface back.  Called when the context that owns it goes away,
 * and when a fork leaves a child holding a mapping made through a descriptor
 * that is no longer its own.  Without it the first context to be destroyed
 * took the only surface with it and nothing in the process could accelerate
 * again.
 */
void
OSMGAMesaBufferSoiled(void)
{
    bufDirty = 1;
}

void
OSMGAMesaBufferMirror(void)
{
    const unsigned long *src;
    unsigned long *dst;
    unsigned long y, w;

    if (bufMapped == 0 || bufApp == 0 || !bufDirty)
        return;
    bufDirty = 0;

    /*
     * Row by row, because the surface is laid out at the display's stride
     * and the application's buffer at its own width -- copying the whole
     * thing in one go would slide every row along by the difference.
     *
     * This copies the entire surface every time, which is honest rather than
     * clever: only the triangles this back end drew have a bounding box we
     * know, and the software rasteriser writing into the same surface has
     * none we can see.  Narrowing it needs both halves to report what they
     * touched, and is recorded as work rather than guessed at here.
     */
    src = (const unsigned long *)bufMapped;
    dst = (unsigned long *)bufApp;
    w = bufWidth;
    for (y = 0UL; y < bufHeight; y++) {
        const unsigned long *s = src + y * bufStride;
        unsigned long *d = dst + y * bufAppRow;
        unsigned long x;

        for (x = 0UL; x < w; x++)
            d[x] = s[x];
    }
}

void
OpenStepMesaAccelReleaseBuffer(void *ctx)
{
    (void)ctx;
    if (depthMapped != 0) {
        (void)vm_deallocate(task_self(), (vm_address_t)depthMapped,
                            (vm_size_t)depthBytes);
        depthMapped = 0;
        depthBytes = 0UL;
    }
    depthOrigin = 0UL;
    if (bufMapped != 0) {
        (void)vm_deallocate(task_self(), (vm_address_t)bufMapped,
                            (vm_size_t)bufBytes);
        bufMapped = 0;
        bufBytes = 0UL;
    }
    bufCtx = 0;
    bufApp = 0;
    bufAppRow = 0UL;
    bufDirty = 0;
    bufOrigin = 0UL;
    bufWidth = bufHeight = bufStride = 0UL;
}

void *
OpenStepMesaAccelBuffer(void *ctx, void *buffer, int width, int height,
                        int rshift, int gshift, int bshift, int appRowLength,
                        int *rowLength)
{
    OSMGAMesaProbe probe;
    unsigned long stride, need, avail;
    vm_address_t addr = 0;


    if (width <= 0 || height <= 0 || rowLength == 0)
        return 0;
    /*
     * How the caller's own array is laid out, which is what the copy back
     * has to follow.  It is usually the width, but OSMesaPixelStore lets a
     * caller choose otherwise before making the context current, and
     * assuming the width would then write every row at the wrong offset.
     */
    if (appRowLength <= 0)
        return 0;

    /*
     * Binding again at the same size is the ordinary case and gets the same
     * surface back.  Clearing the description first, as this used to, meant a
     * triangle function installed for the previous binding could submit
     * against an origin of zero and a size of zero -- the description has to
     * stay true for as long as anything might still be drawing through it.
     */
    if (bufMapped != 0) {
        /*
         * There is one surface, and it belongs to the context that got it.
         * A second context was being handed the same memory with only its
         * application pointer swapped in, so two contexts drew over each
         * other and destroying either unmapped the surface the other was
         * still using.  A second context renders in software instead.
         */
        if (bufCtx != ctx)
            return 0;
        if (bufWidth == (unsigned long)width &&
            bufHeight == (unsigned long)height) {
            bufApp = buffer;    /* it may be a different buffer this time */
            bufAppRow = (unsigned long)appRowLength;
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

    /*
     * The engine lays a pixel out as 0x00RRGGBB and cannot be told otherwise,
     * so a caller whose context packs any other way is left with its own
     * buffer.  Sharing a surface with a disagreement about byte order is
     * worse than not sharing it: both paths write plausible pixels and the
     * picture comes out with two different colour orders in it, which is a
     * fault no amount of looking at one triangle will reveal.
     */
    if (rshift != 16 || gshift != 8 || bshift != 0)
        return 0;

    OSMGAMesaProbeRun(&probe);
    if (probe.verdict != OSMGA_PROBE_HARDWARE)
        return 0;

    /*
     * The surface is laid out the way Mesa already lays it out -- at the
     * caller's own row length -- rather than at the display's stride.  The
     * engine used to force the latter, and following it meant overriding
     * Mesa's stride and, worse, that the software rasteriser's depth buffer
     * could never share ours: Mesa addresses depth at the surface's width and
     * has no setting for anything else.  The batch declares the pitch now, so
     * both can simply agree with Mesa.
     */
    stride = (unsigned long)appRowLength;
    avail  = probe.caps[OSMGA_HW3D_CAP_VRAMLEN];
    if (stride == 0UL || (unsigned long)width > stride)
        return 0;               /* a row would run into the next */
    if (stride > probe.caps[OSMGA_HW3D_CAP_STRIDE])
        return 0;               /* wider than the pitch register is known to hold */

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
    bufCtx    = ctx;
    bufApp    = buffer;
    bufAppRow = (unsigned long)appRowLength;
    bufDirty  = 0;
    bufBytes  = need;
    bufOrigin = probe.caps[OSMGA_HW3D_CAP_VRAMOFF];
    bufWidth  = (unsigned long)width;
    bufHeight = (unsigned long)height;
    bufStride = stride;

    /*
     * Nothing to override: Mesa is already laying the surface out this way,
     * and the batch will tell the engine to read it the same.  Nothing is
     * flipped either -- OSMesa puts GL row y at base + y * pitch by default
     * and the engine draws its rows from the origin the same way, so the two
     * agree without being told to.
     */
    *rowLength = 0;
    return bufMapped;
}
