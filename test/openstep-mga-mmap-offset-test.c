/*
 * Does mmap's offset reach this device?  (REMAINING_WORK 3-19)
 *
 * Two clients have mapped the driver's VRAM window at a non-zero offset and
 * behaved as though they had been given the window's start instead.  That was
 * noticed sideways, while chasing something else, and it has since become
 * load-bearing: the Mesa back end maps its depth buffer at a non-zero offset
 * and hands the result to the rasteriser as gl_buffer->DepthBuffer, so if the
 * offset is discarded the software depth path writes over the colour surface.
 *
 * The earlier observation is not good enough to act on, for two reasons.  It
 * inferred the mapping's position from where a rectangle appeared to land,
 * which is a long chain; and both helpers throw away what mmap() returns and
 * hand back the address they had asked for, so nobody has ever looked at the
 * answer.
 *
 * So: three values, not one.  Put different patterns at the two candidate
 * places, write a third through the pointer mmap ACTUALLY returned for an
 * offset mapping, and see which of the two moved.  One marker would let an
 * old value that happened to match say the offset works.
 *
 * Everything written here is put back.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <mach/mach.h>

#define DEV_PATH    "/dev/osmgavram"

/* Both from the driver's own log line, not assumed:
 *   "VRAM window 4194304..7340031 (3072 KiB)" */
#define WIN_START   4194304UL
#define WIN_LEN     3145728UL

/* Far enough in to be clear of every other test's rectangle (they use the
 * first 491520 bytes), a whole number of 8192-byte pages, and inside the
 * window with room to spare.  Checked with python before it was written. */
#define PROBE_OFF   1048576UL

#define PAT_LOW     0xA1A1A1A1UL    /* what sits at the window start */
#define PAT_FAR     0xB2B2B2B2UL    /* what sits PROBE_OFF bytes in */
#define PAT_NEW     0xC3C3C3C3UL    /* written through the offset mapping */

/*
 * What mmap returns here is a STATUS, not an address.
 *
 * This program was first written to keep the return value and use it as the
 * pointer, on the reasoning that answering with the address we asked for
 * would be answering the question with the question.  It returned 0 for a
 * mapping that plainly worked, and dereferencing that got a bus error.  So
 * this is the older Mach form: it maps at the address it was given and
 * reports 0 or -1.  The other clients here hand back the address they asked
 * for, and that is right rather than careless.
 *
 * The offset question therefore has to be answered by looking at memory, not
 * at a return value -- which is what the three markers below are for.
 */
static caddr_t
mapAt(int fd, unsigned long offset, unsigned long len, int *status)
{
    vm_address_t addr = 0;

    if (vm_allocate(task_self(), &addr, (vm_size_t)len, TRUE) != KERN_SUCCESS)
        return (caddr_t)-1;
    *status = (int)mmap((caddr_t)addr, (int)len, PROT_READ | PROT_WRITE,
                        MAP_SHARED, fd, (long)offset);
    if (*status == -1) {
        (void)vm_deallocate(task_self(), addr, (vm_size_t)len);
        return (caddr_t)-1;
    }
    return (caddr_t)addr;
}

int
main(void)
{
    int fd;
    caddr_t aRaw, bRaw;
    int stA = 0, stB = 0;
    volatile unsigned long *A, *B;
    unsigned long far = PROBE_OFF / 4UL;
    unsigned long keepLow, keepFar, sawLow, sawFar;
    int verdict = 0;

    if ((fd = open(DEV_PATH, O_RDWR)) < 0) {
        printf("no %s (%s)\n", DEV_PATH, strerror(errno));
        return 1;
    }

    /* One mapping covering both candidate places, taken at the window start
     * so that its own offset is zero and cannot itself be in question. */
    aRaw = mapAt(fd, WIN_START, PROBE_OFF + 8192UL, &stA);
    if ((int)aRaw == -1) { printf("the base mapping failed\n"); return 1; }
    A = (volatile unsigned long *)aRaw;
    printf("base mapping at offset 0 : at %p, mmap reported %d\n",
           (void *)aRaw, stA);

    keepLow = A[0];
    keepFar = A[far];
    A[0]   = PAT_LOW;
    A[far] = PAT_FAR;
    /* A read past the window's first 64 bytes, because a read inside them
     * leaves the next writes' visibility in question -- see M1-3i. */
    (void)A[16];

    /* Now the mapping under test. */
    bRaw = mapAt(fd, WIN_START + PROBE_OFF, 8192UL, &stB);
    if ((int)bRaw == -1) {
        printf("the offset mapping was REFUSED (%s)\n", strerror(errno));
        printf("   -- that is an answer too: the offset reaches the driver "
               "and something rejected it, rather than being discarded\n");
        A[0] = keepLow; A[far] = keepFar; (void)A[16];
        return 1;
    }
    B = (volatile unsigned long *)bRaw;
    printf("offset mapping at +%lu: at %p, mmap reported %d\n",
           PROBE_OFF, (void *)bRaw, stB);

    B[0] = PAT_NEW;
    (void)B[16];

    sawLow = A[0];
    sawFar = A[far];
    printf("\n   window byte 0        holds %08lx (put there: %08lx)\n",
           sawLow, PAT_LOW);
    printf("   window byte %lu holds %08lx (put there: %08lx)\n",
           PROBE_OFF, sawFar, PAT_FAR);
    printf("   written through the offset mapping: %08lx\n\n", PAT_NEW);

    if (sawFar == PAT_NEW && sawLow == PAT_LOW) {
        printf("VERDICT -- the offset is honoured: the write landed %lu "
               "bytes in, where it was aimed\n", PROBE_OFF);
        verdict = 0;
    } else if (sawLow == PAT_NEW && sawFar == PAT_FAR) {
        printf("VERDICT -- the offset is DISCARDED: the write landed at the "
               "window start, and 3-19 is real\n");
        verdict = 1;
    } else {
        printf("VERDICT -- neither: something else is going on, and no "
               "conclusion is available from this\n");
        verdict = 2;
    }

    /*
     * And the same question for an offset that is NOT a whole number of
     * pages.  The earlier belief that offsets were discarded came from a
     * client mapping 4096 bytes in on a machine whose page is 8192, and the
     * two possible answers -- refused, or quietly rounded down to the page --
     * look nothing alike to a caller and identical to the experiment that
     * was run.
     */
    {
        caddr_t cRaw;
        int stC = 0;
        unsigned long half = 4096UL;

        cRaw = mapAt(fd, WIN_START + half, 8192UL, &stC);
        if ((int)cRaw == -1) {
            printf("\n   an offset of %lu, which is half a page here, was "
                   "REFUSED (%s)\n", half, strerror(errno));
        } else {
            volatile unsigned long *C = (volatile unsigned long *)cRaw;
            unsigned long marker = 0xD4D4D4D4UL, atLow, atHalf;

            C[0] = marker;
            (void)A[16];
            atLow  = A[0];
            atHalf = A[half / 4UL];
            printf("\n   an offset of %lu (half a page) was ACCEPTED\n", half);
            printf("      window byte 0    now holds %08lx\n", atLow);
            printf("      window byte %lu now holds %08lx\n", half, atHalf);
            if (atHalf == marker)
                printf("      -> honoured even unaligned\n");
            else if (atLow == marker)
                printf("      -> ROUNDED DOWN to the page: this is what the "
                       "earlier client saw and read as 'ignored'\n");
            else
                printf("      -> neither; no conclusion\n");
            A[0] = keepLow;
            A[half / 4UL] = keepFar == 0UL ? 0UL : A[half / 4UL];
        }
    }

    A[0] = keepLow; A[far] = keepFar; (void)A[16];
    (void)close(fd);
    return verdict;
}
