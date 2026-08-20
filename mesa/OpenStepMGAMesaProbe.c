/*
 * OpenStepMGAMesaProbe.c - M1-3a: decide, once, whether to accelerate.
 *
 * See OpenStepMGAMesaProbe.h.  Plain C, no Objective-C, no Mesa headers: this
 * file must be readable on its own, because whether an application gets
 * hardware or software is decided here and nowhere else.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/file.h>
#include <sys/stat.h>

#include "OpenStepMGAMesaProbe.h"

extern int open(const char *, int, ...);
extern int close(int);
/* Exactly as <libc.h> declares it.  A locally invented prototype would
 * conflict with the real one, and "it is ABI-compatible on this target"
 * is not the same as "it is a valid declaration". */
extern int ioctl(int, long, ...);

#define OSMGA_PROBE_DEV "/dev/osmgavram"

/*
 * The override can only ever turn acceleration OFF.  There is no value of it
 * that forces hardware on, because that would let an environment variable
 * defeat the Configure.app switch and the driver's own report of what it can
 * do.  Being one-directional also makes it safe to leave in a shipped
 * library: the worst it can do is what happens with no driver installed.
 *
 * It exists because of the reason the plan gives for building one library
 * instead of two -- with two, the software path stops being exercised.  This
 * lets the software path be run on the machine that has the hardware, and
 * without a reboot.
 */
#define OSMGA_PROBE_ENV "OSMGA_MESA_ACCEL"

static int probeDone;
static OSMGAMesaProbe probeCached;

static int
osmgaProbeForcedOff(void)
{
    const char *v = getenv(OSMGA_PROBE_ENV);

    if (v == 0 || *v == '\0')
        return 0;
    return (*v == '0' || *v == 'n' || *v == 'N' ||
            *v == 'f' || *v == 'F') ? 1 : 0;
}

static void
osmgaProbePerform(OSMGAMesaProbe *p)
{
    OSMGAHW3DCapsBlock blk;
    struct stat st;
    unsigned long flags;
    unsigned i;
    int fd;

    p->fd = -1;
    p->missing = 0UL;
    p->nodeMajor = 0UL;
    for (i = 0U; i < OSMGA_HW3D_CAPS_COUNT; i++)
        p->caps[i] = 0UL;

    if (osmgaProbeForcedOff()) {
        p->verdict = OSMGA_PROBE_OVERRIDE;
        return;
    }

    if ((fd = open(OSMGA_PROBE_DEV, O_RDWR)) < 0) {
        p->verdict = OSMGA_PROBE_NO_DEVICE;
        return;
    }
    /* Written out rather than using major(), which <sys/types.h> defines
     * inside a conditional; the shift and mask are that macro's own:
     *   #define major(x) ((int)(((unsigned)(x)>>8)&0377)) */
    if (fstat(fd, &st) == 0)
        p->nodeMajor = (unsigned long)(((unsigned)st.st_rdev >> 8) & 0xFFU);

    /*
     * Asking is the identity check.  Another display driver does not know
     * this command, so it refuses, and no separate way of telling whose card
     * this is has to exist or be kept correct.
     */
    if (ioctl(fd, (long)OSMGA_IOC_CAPS, &blk) < 0) {
        (void)close(fd);
        p->verdict = OSMGA_PROBE_NOT_OURS;
        return;
    }

    for (i = 0U; i < OSMGA_HW3D_CAPS_COUNT; i++)
        p->caps[i] = blk.caps[i];

    if (p->caps[OSMGA_HW3D_CAP_MAGIC] != OSMGA_HW3D_MAGIC) {
        (void)close(fd);
        p->verdict = OSMGA_PROBE_MAGIC;
        return;
    }
    /*
     * Exact equality, not "at least".  The batch is a shared memory layout,
     * so a driver and a library that disagree about its version disagree
     * about where the fields are, and drawing through that mismatch would
     * corrupt whatever the offsets happen to land on.
     */
    if (p->caps[OSMGA_HW3D_CAP_VERSION] != OSMGA_HW3D_VERSION) {
        (void)close(fd);
        p->verdict = OSMGA_PROBE_VERSION;
        return;
    }

    /*
     * The device node is created by hand and its major is assigned
     * dynamically, so a node left over from an earlier boot can name a major
     * that now belongs to something else.  We have been bitten by exactly
     * that.  If the driver we reached reports a major other than the one we
     * opened, the node is not describing this driver and nothing built on it
     * can be trusted.
     */
    if (p->nodeMajor != 0UL &&
        p->caps[OSMGA_HW3D_CAP_MAJOR] != p->nodeMajor) {
        (void)close(fd);
        p->verdict = OSMGA_PROBE_STALE_NODE;
        return;
    }

    flags = p->caps[OSMGA_HW3D_CAP_FLAGS];
    if ((flags & OSMGA_HW3D_CAP_ENABLED) == 0UL) {
        (void)close(fd);
        p->verdict = OSMGA_PROBE_DISABLED;
        return;
    }
    if ((flags & OSMGA_HW3D_CAP_REQUIRED) != OSMGA_HW3D_CAP_REQUIRED) {
        p->missing = OSMGA_HW3D_CAP_REQUIRED & ~flags;
        (void)close(fd);
        p->verdict = OSMGA_PROBE_UNAVAILABLE;
        return;
    }

    p->fd = fd;
    p->verdict = OSMGA_PROBE_HARDWARE;
}

void
OSMGAMesaProbeRun(OSMGAMesaProbe *out_probe)
{
    if (out_probe == 0)
        return;
    if (!probeDone) {
        osmgaProbePerform(&probeCached);
        probeDone = 1;
    }
    *out_probe = probeCached;
}

void
OSMGAMesaProbeRevoke(const char *why)
{
    /*
     * Revoking before the probe has run still has to stick, so mark it done
     * rather than leaving a later probe free to decide hardware.
     */
    if (probeDone && probeCached.verdict != OSMGA_PROBE_HARDWARE)
        return;
    if (probeDone && probeCached.fd >= 0) {
        (void)close(probeCached.fd);
        probeCached.fd = -1;
    }
    probeCached.verdict = OSMGA_PROBE_REVOKED;
    probeDone = 1;
    fprintf(stderr, "OpenStepMGA: hardware acceleration revoked (%s); "
                    "rendering in software from here on\n",
            (why != 0) ? why : "no reason given");
}

const char *
OSMGAMesaProbeVerdictString(OSMGAProbeVerdict v)
{
    switch (v) {
    case OSMGA_PROBE_HARDWARE:    return "hardware";
    case OSMGA_PROBE_NO_DEVICE:   return "software: no device";
    case OSMGA_PROBE_NOT_OURS:    return "software: not our driver";
    case OSMGA_PROBE_MAGIC:       return "software: magic mismatch";
    case OSMGA_PROBE_VERSION:     return "software: version skew";
    case OSMGA_PROBE_DISABLED:    return "software: switched off";
    case OSMGA_PROBE_UNAVAILABLE: return "software: 3D path unavailable";
    case OSMGA_PROBE_OVERRIDE:    return "software: forced by environment";
    case OSMGA_PROBE_REVOKED:     return "software: revoked after a failure";
    case OSMGA_PROBE_STALE_NODE:  return "software: stale /dev node";
    }
    return "software: unknown verdict";
}
