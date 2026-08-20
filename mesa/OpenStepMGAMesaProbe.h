/*
 * OpenStepMGAMesaProbe.h - M1-3a: decide, once, whether to accelerate.
 *
 * This is compiled into libGL and must therefore be plain C.  It links no
 * Objective-C: the parameter form of this query needs IODeviceMaster, and an
 * archive containing that class cannot be linked into a C program, which
 * would break the requirement that an application build unchanged with
 * nothing but -lGL.  The capabilities arrive through an ioctl on the
 * character device the library has to open for mmap anyway.
 *
 * Not to be confused with OpenStepMGAMesaAdmission.h next to it, which is an
 * offline policy record from the R3/R6 review track and touches no device.
 */

#ifndef OPENSTEP_MGA_MESA_PROBE_H
#define OPENSTEP_MGA_MESA_PROBE_H

#include "../hw3d/OpenStepMGAHW3D.h"

typedef enum {
    OSMGA_PROBE_HARDWARE = 0,   /* accelerate */
    OSMGA_PROBE_NO_DEVICE,      /* no /dev node: driver absent or not built */
    OSMGA_PROBE_NOT_OURS,       /* device exists but does not know us */
    OSMGA_PROBE_MAGIC,          /* answered, but not with our magic */
    OSMGA_PROBE_VERSION,        /* package skew: driver and library disagree */
    OSMGA_PROBE_DISABLED,       /* Configure.app switch is off */
    OSMGA_PROBE_UNAVAILABLE,    /* switch on, but the 3D path is not usable */
    OSMGA_PROBE_OVERRIDE,       /* forced off through the environment */
    OSMGA_PROBE_REVOKED,        /* was accelerating; something went wrong */
    OSMGA_PROBE_STALE_NODE      /* /dev node names a major this driver is not */
} OSMGAProbeVerdict;

typedef struct {
    OSMGAProbeVerdict verdict;
    unsigned long caps[OSMGA_HW3D_CAPS_COUNT];
    unsigned long missing;      /* required bits absent, when UNAVAILABLE */
    unsigned long nodeMajor;    /* major of the node we opened, 0 if unknown */
    int fd;                     /* the open device, or -1 */
} OSMGAMesaProbe;

/*
 * Runs at most once per process and caches the answer; later calls copy it.
 * Never fails: an unusable card is a verdict, not an error, because every
 * caller's response to failure is the same -- render in software.
 */
void OSMGAMesaProbeRun(OSMGAMesaProbe *out_probe);

/*
 * Give up on acceleration for the rest of the process.  One-way on purpose:
 * a path that could turn hardware back on would have to prove the card is
 * in a known state, and nothing that has just gone wrong can prove that.
 */
void OSMGAMesaProbeRevoke(const char *why);

const char *OSMGAMesaProbeVerdictString(OSMGAProbeVerdict v);

#endif /* OPENSTEP_MGA_MESA_PROBE_H */
