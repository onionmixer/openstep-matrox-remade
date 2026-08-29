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

/* By bare name, as OpenStepMGAMesaHook.h does.  A relative path works only
 * in this tree: once the header is installed beside its siblings in a
 * prefix's Headers there is no ../hw3d, and a shipped demo that includes
 * this fails to compile.  In-tree builds pass -I../hw3d already. */
#include "OpenStepMGAHW3D.h"

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

/*
 * No file descriptor here, deliberately.  An earlier version returned the
 * open device in this structure, and because the structure is copied to every
 * caller, they all held the same number with no rule about who owned it: one
 * caller closing it left the others using a descriptor that the next open()
 * in the process could hand to an unrelated file.  The descriptor stays
 * private and is reached through the accessor below, which lends it.
 */
typedef struct {
    OSMGAProbeVerdict verdict;
    unsigned long caps[OSMGA_HW3D_CAPS_COUNT];
    unsigned long missing;      /* required bits absent, when UNAVAILABLE */
    unsigned long nodeMajor;    /* major of the node we opened, 0 if unknown */
} OSMGAMesaProbe;

/*
 * Runs at most once per process and caches the answer; later calls copy it.
 * Never fails: an unusable card is a verdict, not an error, because every
 * caller's response to failure is the same -- render in software.
 *
 * The environment override is therefore sampled at the FIRST call, not at
 * some defined moment of library loading, and is fixed from then on.  An
 * application that wants to force software has to set the variable before any
 * GL call, which in practice means before it starts.
 *
 * Across fork() the answer is recomputed rather than inherited, because the
 * parent's open device came along with the child and two processes sharing
 * one submission channel is not something the driver contract allows.
 */
void OSMGAMesaProbeRun(OSMGAMesaProbe *out_probe);

/*
 * Give up on acceleration for the rest of the process.  One-way on purpose:
 * a path that could turn hardware back on would have to prove the card is
 * in a known state, and nothing that has just gone wrong can prove that.
 */
void OSMGAMesaProbeRevoke(const char *why);

/*
 * The open device, or -1.  Borrowed, never owned: the module closes it and
 * the caller must not.  Only libGL's own backend has any business with it.
 */
int OSMGAMesaProbeDeviceFd(void);

/*
 * The shared command window, or NULL.  Mapped once and returned to every
 * caller thereafter; the batch lives at its start.  Only meaningful after a
 * probe has said hardware.
 */
OSMGAHW3DBatch *OSMGAMesaProbeBatch(void);

/*
 * Run whatever is in that batch.  Returns 0 when it drew, a positive errno
 * when the driver ran it and declined -- `result` then names the triangle
 * and the reason -- and -1 when the driver never attempted it at all, where
 * `result` holds nothing but the verdict OSMGA_HW3D_NOT_RUN.
 *
 * A refusal is the caller's cue to stop asking: this does not revoke by
 * itself, because a batch the library built wrongly is a different thing
 * from hardware that has stopped working, and only the caller knows which
 * it just did.
 */
int OSMGAMesaProbeSubmit(OSMGAHW3DSubmitBlock *result);

/*
 * MEASUREMENT ONLY.  Same batch, same validation, same encoding -- and then
 * the driver returns instead of ringing the doorbell.  It exists to separate
 * the kernel's per-trapezoid work from the engine's.
 */
/* Compiled only where the ioctl itself is: OSMGA_HW3D_SUBMIT_DRY gates the
 * command in OpenStepMGAHW3D.h and the handler in the driver, and a wrapper
 * that could name a command nothing answers would only be able to fail. */
#ifdef OSMGA_HW3D_SUBMIT_DRY
int OSMGAMesaProbeSubmitDry(OSMGAHW3DSubmitBlock *result);
#endif

const char *OSMGAMesaProbeVerdictString(OSMGAProbeVerdict v);

#endif /* OPENSTEP_MGA_MESA_PROBE_H */
