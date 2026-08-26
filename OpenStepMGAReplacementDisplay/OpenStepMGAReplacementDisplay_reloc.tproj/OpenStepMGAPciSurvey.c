/* See OpenStepMGAPciSurvey.h.  C89, no libc, no kernel headers. */

#include "OpenStepMGAPciSurvey.h"

#define OSMGA_PCI_MAX_DEVICE    32
#define OSMGA_PCI_BUS_WORDS     8      /* 256 buses, one bit each */

static int
osmgaBusBit(const unsigned long *map, int bus)
{
    return (map[(bus >> 5) & 7] & (1UL << (bus & 31))) != 0UL;
}

static void
osmgaBusSet(unsigned long *map, int bus)
{
    map[(bus >> 5) & 7] |= 1UL << (bus & 31);
}

static int
osmgaPciAbsent(unsigned long vendorDevice)
{
    return vendorDevice == 0xFFFFFFFFUL || vendorDevice == 0UL;
}

static void
osmgaPciEmit(OSMGAPciEmitFn emit, void *ctx, int bus, int dev, int fn,
             int isSelf, int isAncestor, OSMGAPciEventKind kind,
             unsigned long a, unsigned long b)
{
    OSMGAPciEvent ev;

    if (emit == 0)
        return;
    ev.bus = bus;
    ev.dev = dev;
    ev.fn = fn;
    ev.isSelf = isSelf;
    ev.isAncestor = isAncestor;
    ev.kind = kind;
    ev.a = a;
    ev.b = b;
    emit(ctx, &ev);
}

/*
 * One base address register.  Returns how many dwords it consumed, or 0 when
 * it claims to be a pair and there is no second dword to pair with.
 */
static int
osmgaPciOneBar(OSMGAPciReadFn read, void *rctx, int bus, int dev, int fn,
               int reg, int lastReg, int isSelf, OSMGASurveyState *st,
               OSMGAPciEmitFn emit, void *ectx)
{
    unsigned long v = read(rctx, bus, dev, fn, reg);
    unsigned long type;
    unsigned long base;

    if ((v & 1UL) != 0UL)
        return 1;                       /* an I/O BAR decodes no memory */

    type = (v >> 1) & 3UL;
    if (type == 3UL) {
        /* Reserved.  Refused rather than guessed: a type this driver does
         * not know is not a type it may bound. */
        OSMGASurveyRefuse(st, OSMGA_SURVEY_MALFORMED, v);
        osmgaPciEmit(emit, ectx, bus, dev, fn, isSelf, 0,
                     OSMGA_PCI_EV_MALFORMED, v, 0UL);
        return 1;
    }
    if (type == 2UL) {
        unsigned long hi;

        if (reg + 4 > lastReg) {
            OSMGASurveyRefuse(st, OSMGA_SURVEY_MALFORMED, (unsigned long)reg);
            osmgaPciEmit(emit, ectx, bus, dev, fn, isSelf, 0,
                         OSMGA_PCI_EV_MALFORMED, (unsigned long)reg, 0UL);
            return 0;
        }
        hi = read(rctx, bus, dev, fn, reg + 4);
        if (hi != 0UL)
            return 2;                   /* above four gigabytes; out of reach */
        base = v & 0xFFFFFFF0UL;
        if (base != 0UL) {
            if (!isSelf)
                OSMGASurveyClaimBase(st, base);
            osmgaPciEmit(emit, ectx, bus, dev, fn, isSelf, 0,
                         OSMGA_PCI_EV_BASE, base,
                         OSMGASurveyAlignmentBound(base));
        }
        return 2;
    }

    base = v & 0xFFFFFFF0UL;
    /*
     * A base register left at zero was never assigned by the firmware.  It
     * decodes nothing, so it is neither a claim nor worth a log line -- and
     * most functions have several, so emitting them buries the ones that
     * matter.
     */
    if (base != 0UL) {
        if (!isSelf)
            OSMGASurveyClaimBase(st, base);
        osmgaPciEmit(emit, ectx, bus, dev, fn, isSelf, 0, OSMGA_PCI_EV_BASE,
                     base, OSMGASurveyAlignmentBound(base));
    }
    return 1;
}

static void
osmgaPciBridgeWindows(OSMGAPciReadFn read, void *rctx, int bus, int dev,
                      int fn, int isAncestor, OSMGASurveyState *st,
                      OSMGAPciEmitFn emit, void *ectx)
{
    unsigned long v;
    unsigned long base;
    unsigned long limit;

    /* Non-prefetchable memory window: 1 MiB granularity, limit inclusive. */
    v = read(rctx, bus, dev, fn, 0x20);
    base = (v & 0x0000FFF0UL) << 16;
    limit = ((v >> 16) & 0xFFF0UL) << 16;
    /*
     * base > limit is the disabled encoding, and a base of zero is a window
     * nobody assigned: physical zero is main memory, not something a bridge
     * forwards.  Without the second test an unconfigured bridge reads as a
     * one-megabyte window at address zero.
     */
    if (base != 0UL && base <= limit) {
        OSMGASurveyClaimWindow(st, base, limit + 0x100000UL, isAncestor);
        osmgaPciEmit(emit, ectx, bus, dev, fn, 0, isAncestor,
                     OSMGA_PCI_EV_WINDOW, base, limit + 0x100000UL);
    }

    /*
     * The prefetchable window, which is the one that matters here: H1 S1
     * records this card's BAR0 as prefetchable, so it is through THIS window
     * that an ancestor forwards the aperture.  It may be 64 bits wide, and
     * then a non-zero upper half puts it out of a 32-bit range's reach.
     */
    v = read(rctx, bus, dev, fn, 0x24);
    if ((v & 0x000FUL) == 1UL) {
        if (read(rctx, bus, dev, fn, 0x28) != 0UL ||
            read(rctx, bus, dev, fn, 0x2c) != 0UL)
            return;
    } else if ((v & 0x000FUL) != 0UL) {
        OSMGASurveyRefuse(st, OSMGA_SURVEY_MALFORMED, v);
        osmgaPciEmit(emit, ectx, bus, dev, fn, 0, isAncestor,
                     OSMGA_PCI_EV_MALFORMED, v, 0UL);
        return;
    }
    base = (v & 0x0000FFF0UL) << 16;
    limit = ((v >> 16) & 0xFFF0UL) << 16;
    if (base != 0UL && base <= limit) {
        OSMGASurveyClaimWindow(st, base, limit + 0x100000UL, isAncestor);
        osmgaPciEmit(emit, ectx, bus, dev, fn, 0, isAncestor,
                     OSMGA_PCI_EV_WINDOW, base, limit + 0x100000UL);
    }
}

void
OSMGAPciSurveyRun(OSMGAPciReadFn read, void *readCtx,
                  int ourBus, int ourDev, int ourFn,
                  unsigned long fbPhysical, unsigned long wantBytes,
                  OSMGAPciEmitFn emit, void *emitCtx,
                  OSMGASurveyState *st)
{
    unsigned long todo[OSMGA_PCI_BUS_WORDS];
    unsigned long done[OSMGA_PCI_BUS_WORDS];
    int i;
    int rounds;

    if (st == 0)
        return;
    OSMGASurveyBegin(st, fbPhysical, wantBytes);
    if (read == 0) {
        OSMGASurveyRefuse(st, OSMGA_SURVEY_MALFORMED, 0UL);
        return;
    }
    for (i = 0; i < OSMGA_PCI_BUS_WORDS; i++) {
        todo[i] = 0UL;
        done[i] = 0UL;
    }
    osmgaBusSet(todo, 0);

    /* Bounded by construction: each bus is walked at most once and there are
     * 256 of them, so the outer loop cannot run longer than that. */
    for (rounds = 0; rounds < 256; rounds++) {
        int bus = -1;
        int dev;

        for (i = 0; i < 256; i++)
            if (osmgaBusBit(todo, i) && !osmgaBusBit(done, i)) {
                bus = i;
                break;
            }
        if (bus < 0)
            break;
        osmgaBusSet(done, bus);

        for (dev = 0; dev < OSMGA_PCI_MAX_DEVICE; dev++) {
            int functions;
            int fn;

            if (osmgaPciAbsent(read(readCtx, bus, dev, 0, 0x00)))
                continue;
            functions = (((read(readCtx, bus, dev, 0, 0x0c) >> 16) & 0x80UL)
                         != 0UL) ? 8 : 1;

            for (fn = 0; fn < functions; fn++) {
                unsigned long header;
                int isSelf;
                int nbars;
                int romReg;
                int reg;
                unsigned long vd = read(readCtx, bus, dev, fn, 0x00);

                if (osmgaPciAbsent(vd))
                    continue;
                /* Bit 7 is the multifunction flag, not part of the type. */
                header = (read(readCtx, bus, dev, fn, 0x0c) >> 16) & 0x7FUL;
                isSelf = (bus == ourBus && dev == ourDev && fn == ourFn)
                       ? 1 : 0;
                if (isSelf)
                    osmgaPciEmit(emit, emitCtx, bus, dev, fn, 1, 0,
                                 OSMGA_PCI_EV_SELF, vd, 0UL);

                if (header == 0x00UL) {
                    nbars = 6;
                    romReg = 0x30;
                } else if (header == 0x01UL) {
                    unsigned long buses = read(readCtx, bus, dev, fn, 0x18);
                    int secondary = (int)((buses >> 8) & 0xFFUL);
                    int subordinate = (int)((buses >> 16) & 0xFFUL);
                    int isAncestor = (secondary <= ourBus &&
                                      ourBus <= subordinate) ? 1 : 0;

                    nbars = 2;
                    romReg = 0x38;
                    if (secondary > 0)
                        osmgaBusSet(todo, secondary);
                    osmgaPciEmit(emit, emitCtx, bus, dev, fn, isSelf,
                                 isAncestor, OSMGA_PCI_EV_BRIDGE,
                                 (unsigned long)secondary,
                                 (unsigned long)subordinate);
                    osmgaPciBridgeWindows(read, readCtx, bus, dev, fn,
                                          isAncestor, st, emit, emitCtx);
                } else {
                    /* Cardbus, or something newer than this driver.  A layout
                     * it cannot parse is one it may not reason about. */
                    OSMGASurveyRefuse(st, OSMGA_SURVEY_UNKNOWN_HEADER, header);
                    osmgaPciEmit(emit, emitCtx, bus, dev, fn, isSelf, 0,
                                 OSMGA_PCI_EV_UNKNOWN_HEADER, header, 0UL);
                    continue;
                }

                reg = 0x10;
                while (reg < 0x10 + 4 * nbars) {
                    int used = osmgaPciOneBar(read, readCtx, bus, dev, fn, reg,
                                              0x10 + 4 * nbars - 4, isSelf,
                                              st, emit, emitCtx);
                    if (used == 0)
                        break;
                    reg += 4 * used;
                }

                {
                    unsigned long rom = read(readCtx, bus, dev, fn, romReg);

                    /* A disabled expansion ROM decodes nothing. */
                    if ((rom & 1UL) != 0UL) {
                        unsigned long base = rom & 0xFFFFF800UL;

                        if (!isSelf)
                            OSMGASurveyClaimBase(st, base);
                        osmgaPciEmit(emit, emitCtx, bus, dev, fn, isSelf, 0,
                                     OSMGA_PCI_EV_BASE, base,
                                     OSMGASurveyAlignmentBound(base));
                    }
                }
            }
        }
    }
}
