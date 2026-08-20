/*
 * openstep-mga-caps-client.c - M1-3a: ask the driver what it can do, from C.
 *
 * This is the probe the Mesa backend will perform, kept as a standalone
 * program so the decision can be watched without a GL context in the way.
 *
 * Deliberately plain C with no Objective-C anywhere.  The parameter form of
 * the same query needs IODeviceMaster, and an archive holding that class
 * cannot be linked into a C program -- so libGL cannot use it, and neither
 * does this.  Build with:  cc -O -Wall -o caps openstep-mga-caps-client.c
 * and note the absence of -lDriver: that absence is the point.
 */

#include <stdio.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/file.h>

#include "../hw3d/OpenStepMGAHW3D.h"

extern int open(const char *, int, ...);
extern int close(int);
extern int ioctl(int, unsigned long, void *);

#define DEV_PATH "/dev/osmgavram"

static void
report(const char *name, unsigned long flags, unsigned long bit)
{
    printf("  %-8s %s\n", name, (flags & bit) ? "yes" : "NO");
}

int
main(void)
{
    OSMGAHW3DCapsBlock blk;
    unsigned long flags;
    int fd, i;

    for (i = 0; i < (int)OSMGA_HW3D_CAPS_COUNT; i++)
        blk.caps[i] = 0xDEADBEEFUL;

    if ((fd = open(DEV_PATH, O_RDWR)) < 0) {
        printf("caps: open %s failed (errno %d)\n", DEV_PATH, errno);
        printf("VERDICT: software (no device)\n");
        return 1;
    }
    if (ioctl(fd, (unsigned long)OSMGA_IOC_CAPS, &blk) < 0) {
        printf("caps: ioctl failed (errno %d)\n", errno);
        printf("VERDICT: software (not our driver)\n");
        (void)close(fd);
        return 1;
    }
    (void)close(fd);

    /*
     * A command we never registered must be refused rather than answered
     * from whatever the previous caller left behind.  This costs nothing to
     * check here and would be invisible in the positive test above.
     */
    if ((fd = open(DEV_PATH, O_RDWR)) >= 0) {
        unsigned long bogus = (unsigned long)OSMGA_IOC_CAPS ^ 0x00000002UL;
        int rc = ioctl(fd, bogus, &blk);

        printf("caps: unknown command %08lx -> %s (errno %d)\n",
               bogus, (rc < 0) ? "refused" : "ANSWERED, WRONG", errno);
        (void)close(fd);
        if (rc >= 0)
            printf("FAIL: an unregistered command was answered\n");
    }

    printf("caps: cmd=%08lx size=%lu\n",
           (unsigned long)OSMGA_IOC_CAPS,
           (unsigned long)sizeof(OSMGAHW3DCapsBlock));
    printf("  magic   %08lx (want %08lx)\n",
           blk.caps[OSMGA_HW3D_CAP_MAGIC], OSMGA_HW3D_MAGIC);
    printf("  version %lu (want %lu)\n",
           blk.caps[OSMGA_HW3D_CAP_VERSION], OSMGA_HW3D_VERSION);
    flags = blk.caps[OSMGA_HW3D_CAP_FLAGS];
    printf("  flags   %08lx\n", flags);
    report("ENABLED", flags, OSMGA_HW3D_CAP_ENABLED);
    report("MMAP",    flags, OSMGA_HW3D_CAP_MMAP);
    report("CMD",     flags, OSMGA_HW3D_CAP_CMD);
    report("READY",   flags, OSMGA_HW3D_CAP_READY);
    printf("  maxTri  %lu\n",  blk.caps[OSMGA_HW3D_CAP_MAXTRI]);
    printf("  batch   %lu\n",  blk.caps[OSMGA_HW3D_CAP_BATCH]);
    printf("  major   %lu\n",  blk.caps[OSMGA_HW3D_CAP_MAJOR]);
    printf("  vram    +%lu, %lu bytes\n",
           blk.caps[OSMGA_HW3D_CAP_VRAMOFF], blk.caps[OSMGA_HW3D_CAP_VRAMLEN]);

    if (blk.caps[OSMGA_HW3D_CAP_MAGIC] != OSMGA_HW3D_MAGIC) {
        printf("VERDICT: software (magic mismatch)\n");
        return 1;
    }
    if (blk.caps[OSMGA_HW3D_CAP_VERSION] != OSMGA_HW3D_VERSION) {
        printf("VERDICT: software (version skew)\n");
        return 1;
    }
    if ((flags & OSMGA_HW3D_CAP_REQUIRED) != OSMGA_HW3D_CAP_REQUIRED) {
        printf("VERDICT: software (missing %08lx)\n",
               OSMGA_HW3D_CAP_REQUIRED & ~flags);
        return 1;
    }
    printf("VERDICT: hardware\n");
    return 0;
}
