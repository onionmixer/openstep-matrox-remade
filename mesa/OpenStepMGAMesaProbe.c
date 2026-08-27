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
#include <sys/fcntl.h>
#include <mach/mach.h>

#include "OpenStepMGAMesaProbe.h"
#include "OpenStepMGAMesaBuffer.h"

extern int open(const char *, int, ...);
extern int close(int);
extern int fcntl(int, int, ...);
extern int getpid(void);
extern caddr_t mmap(caddr_t, int, int, int, int, long);

#define PROT_READ   0x01
#define PROT_WRITE  0x02
#define MAP_SHARED  0x0001

/* Must match the driver's second window. */
#define OSMGA_CMD_WINDOW_BASE 0x40000000UL
/* The batch only.  The kernel builds its command list in the rest of that
 * allocation and does not let it be mapped -- a client able to rewrite the
 * list after validation could put anything at all in front of the engine. */
#define OSMGA_CMD_WINDOW_LEN  ((int)OSMGA_HW3D_BATCH_BYTES)
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
static int probeFd = -1;        /* private; see the header */
static int probePid = -1;       /* which process decided this */

/*
 * Revocation lives in its own one-way flag rather than in the cached verdict.
 * Sharing the verdict lost it: a thread revoking while another was still
 * inside the probe would set REVOKED, and the probe would then finish and
 * overwrite it with HARDWARE, so a failure that asked for software got
 * hardware instead.  Checked last, after the copy, this cannot be undone by
 * anything that finishes later.
 */
static int probeRevoked;

/* The command window, at file scope so that revoking can take it back. */
static OSMGAHW3DBatch *probeBatch;

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
    /* An exec'd program has no business holding the card open, and would
     * keep the device busy for a process that does not know it has it. */
    (void)fcntl(fd, F_SETFD, 1);
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

    /*
     * The version is meant to guarantee the shared layout, but the driver
     * reports these two precisely so the guarantee can be checked rather than
     * assumed.  A driver that agrees about the version and disagrees about
     * how big a batch is, or how many triangles fit, disagrees about the
     * layout -- and the contract says that has to fail before drawing, not be
     * discovered during it.
     */
    if (p->caps[OSMGA_HW3D_CAP_MAXTRI] != OSMGA_HW3D_MAX_TRI ||
        p->caps[OSMGA_HW3D_CAP_BATCH]  != OSMGA_HW3D_BATCH_BYTES) {
        (void)close(fd);
        p->verdict = OSMGA_PROBE_VERSION;
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

    probeFd = fd;
    p->verdict = OSMGA_PROBE_HARDWARE;
}

void
OSMGAMesaProbeRun(OSMGAMesaProbe *out_probe)
{
    int pid = getpid();

    if (out_probe == 0)
        return;

    /*
     * fork() copied both the decision and the open device into the child.
     * Two processes submitting through one channel is not something the
     * driver contract allows, so the child starts again -- after letting go
     * of the descriptor it inherited, which is its own copy to close.
     */
    if (probeDone && pid != probePid) {
        /* The mapping came across the fork too, and it refers to the parent's
         * descriptor; letting it survive would have the child filling a batch
         * through a window it no longer has any claim to. */
        if (probeBatch != 0) {
            (void)vm_deallocate(task_self(), (vm_address_t)probeBatch,
                                (vm_size_t)OSMGA_CMD_WINDOW_LEN);
            probeBatch = 0;
        }
        /* The drawing surface came across too, and describes memory reached
         * through a descriptor that is about to be the parent's alone. */
        OpenStepMesaAccelReleaseBuffer(0);
        if (probeFd >= 0) {
            (void)close(probeFd);
            probeFd = -1;
        }
        probeDone = 0;
    }

    if (!probeDone) {
        osmgaProbePerform(&probeCached);
        probePid = pid;
        probeDone = 1;
    }
    *out_probe = probeCached;

    /* Last, so that nothing finishing later can undo it. */
    if (probeRevoked)
        out_probe->verdict = OSMGA_PROBE_REVOKED;
}

void
OSMGAMesaProbeRevoke(const char *why)
{
    if (probeRevoked)
        return;
    probeRevoked = 1;

    /*
     * Closing here is safe in a way returning the number never was: nothing
     * outside this file ever held it, so no caller can be using it and no
     * later open() can hand our number to somebody who thinks it is ours.
     */
    if (probeBatch != 0) {
        (void)vm_deallocate(task_self(), (vm_address_t)probeBatch,
                            (vm_size_t)OSMGA_CMD_WINDOW_LEN);
        probeBatch = 0;
    }
    if (probeFd >= 0) {
        (void)close(probeFd);
        probeFd = -1;
    }
    fprintf(stderr, "OpenStepMGA: hardware acceleration revoked (%s); "
                    "rendering in software from here on\n",
            (why != 0) ? why : "no reason given");
}

int
OSMGAMesaProbeDeviceFd(void)
{
    return probeRevoked ? -1 : probeFd;
}

/*
 * 4.2BSD mmap has neither MAP_FIXED nor "pick an address": it checks that the
 * caller already owns the range and maps over it, so the placeholder has to
 * be allocated first.  Established the hard way by the S4a probe.
 */
static caddr_t
osmgaMapWindow(int fd, unsigned long offset, int len)
{
    vm_address_t addr = 0;

    if (vm_allocate(task_self(), &addr, (vm_size_t)len, TRUE) != KERN_SUCCESS)
        return (caddr_t)-1;
    if ((int)mmap((caddr_t)addr, len, PROT_READ | PROT_WRITE, MAP_SHARED,
                  fd, (long)offset) == -1) {
        (void)vm_deallocate(task_self(), addr, (vm_size_t)len);
        return (caddr_t)-1;
    }
    return (caddr_t)addr;
}

OSMGAHW3DBatch *
OSMGAMesaProbeBatch(void)
{
    caddr_t p;

    /*
     * Revocation first, and only then the cache.  The other order kept
     * handing out a mapping after acceleration had been given up on, and a
     * caller writing through it would still be filling a batch the driver
     * had stopped honouring.
     */
    if (probeRevoked || probeFd < 0)
        return 0;
    if (probeBatch != 0)
        return probeBatch;
    p = osmgaMapWindow(probeFd, OSMGA_CMD_WINDOW_BASE, OSMGA_CMD_WINDOW_LEN);
    if (p == (caddr_t)-1)
        return 0;
    probeBatch = (OSMGAHW3DBatch *)p;
    return probeBatch;
}

/*
 * The two share everything but the command, so they share the body.  A dry
 * submission validates and encodes and then stops; see OSMGA_IOC_SUBMIT_DRY.
 */
static int
osmgaMesaProbeSubmitCmd(OSMGAHW3DSubmitBlock *result, unsigned long cmd)
{
    OSMGAHW3DSubmitBlock scratch;

    if (result == 0)
        result = &scratch;
    result->status = EIO;
    result->verdict = 0UL;
    result->triangle = 0UL;
    result->dwords = 0UL;
    result->spins = 0UL;

    if (probeRevoked || probeFd < 0) {
        result->status = ENXIO;
        return ENXIO;
    }
    if (ioctl(probeFd, (long)cmd, result) < 0) {
        /*
         * The driver never attempted it, so the block was not copied back
         * and holds nothing.  Reported as -1 rather than as the errno,
         * because the errno can equal one the driver itself uses and the
         * caller would otherwise read a completed refusal into it.
         */
        result->status = (errno != 0) ? (unsigned long)errno : EIO;
        result->verdict = OSMGA_HW3D_NOT_RUN;
        return -1;
    }
    return (int)result->status;
}

int
OSMGAMesaProbeSubmit(OSMGAHW3DSubmitBlock *result)
{
    return osmgaMesaProbeSubmitCmd(result, OSMGA_IOC_SUBMIT);
}

/* MEASUREMENT ONLY.  Validates and encodes; never reaches the engine. */
int
OSMGAMesaProbeSubmitDry(OSMGAHW3DSubmitBlock *result)
{
    return osmgaMesaProbeSubmitCmd(result, OSMGA_IOC_SUBMIT_DRY);
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
