/*
 * The PCI walk behind the aperture survey -- separated so it can be tested.
 *
 * Reading configuration space needs port access and belongs in the kernel
 * driver.  Deciding which registers to read, how to parse a header, when a
 * base register is one dword or two, and which bridge is an ancestor does
 * NOT need port access, and it is where the mistakes are: 64-bit base pairs,
 * the multifunction bit, an expansion ROM's enable bit, a prefetchable window
 * that is 64 bits wide.  None of that can be exercised on real hardware
 * without a boot, and a boot is the most expensive test this project has.
 *
 * So the walk takes a read callback and emits structured events, and a host
 * test drives it over a synthetic bus -- including buses this machine does
 * not have.  The driver supplies osmgaPciReadConfigLong and turns the events
 * into IOLog lines.
 *
 * NOTHING HERE WRITES.  There is no write callback, deliberately: the design
 * this replaces sized BAR0 by writing ones to it, and the absence of a way to
 * write is the clearest statement that it no longer does.
 */

#ifndef OPENSTEP_MGA_PCI_SURVEY_H
#define OPENSTEP_MGA_PCI_SURVEY_H

#include "OpenStepMGAWindowMath.h"

/* Reads one aligned dword of configuration space.  Absent functions must
 * come back as 0xFFFFFFFF, which is what mechanism #1 returns for them. */
typedef unsigned long (*OSMGAPciReadFn)(void *ctx, int bus, int dev, int fn,
                                        int reg);

typedef enum {
    OSMGA_PCI_EV_SELF = 0,        /* a: vendor/device */
    OSMGA_PCI_EV_BRIDGE,          /* a: secondary, b: subordinate */
    OSMGA_PCI_EV_BASE,            /* a: base address, b: alignment bound */
    OSMGA_PCI_EV_WINDOW,          /* a: window base, b: window end */
    OSMGA_PCI_EV_UNKNOWN_HEADER,  /* a: header type */
    OSMGA_PCI_EV_MALFORMED        /* a: the register or value at fault */
} OSMGAPciEventKind;

typedef struct {
    int bus;
    int dev;
    int fn;
    int isSelf;
    int isAncestor;
    OSMGAPciEventKind kind;
    unsigned long a;
    unsigned long b;
} OSMGAPciEvent;

typedef void (*OSMGAPciEmitFn)(void *ctx, const OSMGAPciEvent *event);

/*
 * Walks the hierarchy reachable from bus 0, following each bridge's secondary
 * bus rather than probing all 256 -- complete in the sense that a neighbour
 * behind a bridge is still found, while issuing far fewer configuration
 * cycles.  That matters because mechanism #1 is an unserialised two-port
 * latch and every cycle is another chance to be interleaved.
 *
 * emit may be null.  st receives the verdict.
 */
void OSMGAPciSurveyRun(OSMGAPciReadFn read, void *readCtx,
                       int ourBus, int ourDev, int ourFn,
                       unsigned long fbPhysical, unsigned long wantBytes,
                       OSMGAPciEmitFn emit, void *emitCtx,
                       OSMGASurveyState *st);

#endif /* OPENSTEP_MGA_PCI_SURVEY_H */
