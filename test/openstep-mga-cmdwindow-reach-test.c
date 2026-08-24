/*
 * How far does the client's command window actually reach?
 *
 * The driver says, in the mmap handler's own comment, that a client may map
 * the batch and never the ring: "a client that could map it could overwrite
 * the list after it had been validated and before the engine read it".  The
 * check that enforces it is `rel >= OSMGA_HW3D_BATCH_BYTES` on the offset the
 * handler is asked about -- but the kernel maps a whole page, and
 * OSMGA_HW3D_BATCH_BYTES is 28672 while PAGE_SIZE here is 8192.  28672 is
 * three and a half pages.  So the fourth page starts at 24576 and, if the
 * kernel really maps a whole page, it runs to 32768 -- past the batch, into
 * the list, which begins at 28672.
 *
 * This asks the machine rather than the argument, and it asks READ-ONLY.
 * Nothing here writes past the batch; writing there is the thing that would
 * be dangerous, and the point is that it should not be possible, not to find
 * out what happens when it is done.
 *
 * The method: look at the bytes where the list would be, submit a batch
 * through the ordinary path so the kernel encodes a list, and look again.  If
 * they changed, the client is looking at the list.  If they are zero both
 * times, the mapping stops at the batch and the driver is right.
 */
#include <stdio.h>
#include <stdlib.h>
#include <GL/gl.h>
#include <GL/osmesa.h>
#include "OpenStepMGAHW3D.h"
#include "../mesa/OpenStepMGAMesaHook.h"
#include "../mesa/OpenStepMGAMesaBuffer.h"
#include "../mesa/OpenStepMGAMesaProbe.h"

#define W 256
#define H 192
#define LOOK 8                  /* dwords to look at */

int
main(void)
{
    OSMesaContext ctx;
    unsigned long *app;
    OSMGAHW3DBatch *batch;
    const unsigned long *ring;
    unsigned long before[LOOK], after[LOOK];
    unsigned long pageSize = 8192UL;   /* logged by the driver at boot */
    unsigned long lastPage, reach;
    int i, changed = 0, nonzero = 0;

    app = (unsigned long *)malloc((unsigned)(W * H) * sizeof(unsigned long));
    if (!app) { printf("no room\n"); return 2; }
    ctx = OSMesaCreateContext(OSMESA_ARGB, NULL);
    if (!ctx || !OSMesaMakeCurrent(ctx, app, GL_UNSIGNED_BYTE, W, H)) {
        printf("no context\n"); return 2;
    }
    glViewport(0, 0, W, H);

    batch = OSMGAMesaProbeBatch();
    if (batch == 0 || OSMGAMesaBufferOrigin() == 0UL) {
        printf("   no accelerated batch window -- nothing to say\n");
        return 2;
    }

    /*
     * The START of the last page a client could map, which is the largest
     * page-aligned offset strictly below the batch -- not the batch rounded
     * down, which is the same thing only when the batch is NOT a whole
     * number of pages.  Getting that wrong once the split is fixed would
     * make this test read one page past the client's own mapping.
     */
    lastPage = ((OSMGA_HW3D_BATCH_BYTES - 1UL) / pageSize) * pageSize;
    reach    = lastPage + pageSize;

    printf("how far the command window reaches\n\n");
    printf("   PAGE_SIZE                 %lu\n", pageSize);
    printf("   batch bytes (mappable)    %lu\n",
           (unsigned long)OSMGA_HW3D_BATCH_BYTES);
    printf("   the list starts at        %lu\n",
           (unsigned long)OSMGA_HW3D_RING_OFFSET);
    printf("   last page the handler accepts starts at %lu\n", lastPage);
    printf("   ... and a whole page of it ends at      %lu\n", reach);
    if (reach <= (unsigned long)OSMGA_HW3D_RING_OFFSET) {
        printf("\n   the last page a client can map ends at or before the"
               " list, so no mapping can\n   reach it and there is nothing"
               " here to read.  The promise holds.\n");
        OSMesaDestroyContext(ctx);
        free(app);
        return 0;
    }
    printf("   so %lu bytes of the list would be inside the mapping\n\n",
           reach - (unsigned long)OSMGA_HW3D_RING_OFFSET);

    /*
     * Read only.  This address is inside the region the client itself
     * allocated -- vm_allocate rounds a 28672-byte request up to whole pages
     * -- so if the device mapping does NOT reach here, this reads the
     * client's own zero-filled anonymous memory and the answer is "no".
     */
    ring = (const unsigned long *)((const char *)batch +
                                   OSMGA_HW3D_RING_OFFSET);
    for (i = 0; i < LOOK; i++)
        before[i] = ring[i];

    /* Submit something through the ordinary path so the kernel encodes a
     * list.  A clear is enough and touches nothing else. */
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glFinish();

    for (i = 0; i < LOOK; i++)
        after[i] = ring[i];

    printf("   dword   before      after\n");
    for (i = 0; i < LOOK; i++) {
        printf("   %5d   %08lx    %08lx%s\n", i, before[i], after[i],
               (before[i] != after[i]) ? "   <-- changed" : "");
        if (before[i] != after[i]) changed++;
        if (after[i] != 0UL) nonzero++;
    }

    printf("\n   engine clears taken: %lu\n", OSMGAMesaHookClears());
    if (changed > 0 || nonzero > 0) {
        printf("\n   THE CLIENT CAN SEE THE COMMAND LIST.  %d of %d dwords"
               " changed when a batch was\n   submitted, %d are non-zero."
               "  The mapping reaches %lu bytes past the batch,\n   and the"
               " handler's promise that a client may map the batch and never"
               " the ring\n   does not hold at page granularity.\n",
               changed, LOOK, nonzero,
               reach - (unsigned long)OSMGA_HW3D_RING_OFFSET);
        return 1;
    }
    printf("\n   nothing there changed and nothing is set: the mapping does"
           " not reach the list,\n   and the handler's promise holds.\n");
    OSMesaDestroyContext(ctx);
    free(app);
    return 0;
}
