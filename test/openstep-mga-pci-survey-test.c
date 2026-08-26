/*
 * Host regression for the PCI aperture survey, driven over a SYNTHETIC bus.
 *
 * None of this can be exercised on the real machine without a boot, and a
 * boot is the most expensive test this project has.  The synthetic bus is
 * modelled on the real one -- docs/P0_TARGET_INVENTORY.md records the MGA at
 * 04:00.0 behind bridge 03:0d.0, with BAR0 raw 0xf8000008 (prefetchable),
 * BAR1 0xe8200000 and BAR2 0xe8800000 -- and then bent into the shapes the
 * real one cannot produce on demand: a 64-bit base pair, an enabled ROM
 * inside the aperture, a Cardbus header, a parent window that stops short.
 *
 * Every register value was encoded in python first; the bridge window format
 * in particular (1 MiB granularity, inclusive limit, both halves packed into
 * one dword) is not something to derive by eye.
 */

#include <stdio.h>
#include "OpenStepMGAPciSurvey.h"

#define MB      (1024UL * 1024UL)
#define FB      0xf8000000UL
#define WANT32  (32UL * MB)

static int failures;

static void
expect(int condition, const char *label)
{
    if (!condition) {
        printf("OPENSTEP_MGA_PCI_SURVEY_TEST=fail:%s\n", label);
        failures++;
    }
}

/* ------------------------------------------------------ synthetic bus */

typedef struct {
    int bus, dev, fn;
    unsigned long id;          /* 0x00 */
    unsigned long headerByte;  /* byte 2 of 0x0c, multifunction bit included */
    unsigned long bar[6];      /* 0x10..0x24 */
    unsigned long rom;         /* 0x30 for type 0, 0x38 for type 1 */
    unsigned long buses;       /* 0x18, type 1 only */
    unsigned long memwin;      /* 0x20, type 1 only */
    unsigned long prefwin;     /* 0x24, type 1 only */
    unsigned long prefhi;      /* 0x28 and 0x2c, type 1 only */
} FakeFn;

typedef struct {
    const FakeFn *fns;
    unsigned int count;
    unsigned long reads;
} FakeBus;

static unsigned long
fake_read(void *ctx, int bus, int dev, int fn, int reg)
{
    FakeBus *b = (FakeBus *)ctx;
    unsigned int i;

    b->reads++;
    for (i = 0U; i < b->count; i++) {
        const FakeFn *f = &b->fns[i];
        int isBridge;

        if (f->bus != bus || f->dev != dev || f->fn != fn)
            continue;
        isBridge = ((f->headerByte & 0x7FUL) == 0x01UL);
        switch (reg) {
        case 0x00: return f->id;
        case 0x0c: return f->headerByte << 16;
        case 0x10: case 0x14:
            return f->bar[(reg - 0x10) / 4];
        case 0x18: return isBridge ? f->buses : f->bar[2];
        case 0x1c: return isBridge ? 0UL : f->bar[3];
        case 0x20: return isBridge ? f->memwin : f->bar[4];
        case 0x24: return isBridge ? f->prefwin : f->bar[5];
        case 0x28: case 0x2c: return isBridge ? f->prefhi : 0UL;
        case 0x30: return isBridge ? 0UL : f->rom;
        case 0x38: return isBridge ? f->rom : 0UL;
        default: return 0UL;
        }
    }
    return 0xFFFFFFFFUL;       /* what mechanism #1 returns for an empty slot */
}

/*
 * The machine as it is.  Windows encoded in python:
 *   0xf8000000..0xfc000000 -> 0xfbf0f800
 *   0xf8000000..0xfa000000 -> 0xf9f0f800   (exactly 32 MiB)
 *   0xf8000000..0xf9000000 -> 0xf8f0f800   (16 MiB: stops short)
 */
#define WIN_TO_FC   0xfbf0f800UL
#define WIN_TO_FA   0xf9f0f800UL
#define WIN_TO_F9   0xf8f0f800UL

static FakeFn machine[] = {
    /* 00:00.0 host bridge, no BARs */
    { 0, 0, 0, 0x12345678UL, 0x00UL, {0,0,0,0,0,0}, 0, 0, 0, 0, 0 },
    /* 00:1e.0 bridge down to buses 3..4, prefetchable window to 0xfc000000 */
    { 0, 30, 0, 0x24488086UL, 0x01UL, {0,0,0,0,0,0}, 0,
      0x00040300UL, 0UL, WIN_TO_FC, 0UL },
    /* 03:0d.0 HiNT HB4, the recorded upstream bridge, sec=4 sub=4 */
    { 3, 13, 0, 0x00213388UL, 0x01UL, {0,0,0,0,0,0}, 0,
      0x00040403UL, 0UL, WIN_TO_FA, 0UL },
    /* 04:00.0 the MGA itself: BAR0 prefetchable at 0xf8000000 */
    { 4, 0, 0, 0x0525102bUL, 0x00UL,
      { 0xf8000008UL, 0xe8200000UL, 0xe8800000UL, 0, 0, 0 }, 0, 0, 0, 0, 0 }
};

static OSMGASurveyVerdict
run(FakeFn *fns, unsigned int n, unsigned long want, unsigned long *reads)
{
    FakeBus bus;
    OSMGASurveyState st;

    bus.fns = fns;
    bus.count = n;
    bus.reads = 0UL;
    OSMGAPciSurveyRun(fake_read, &bus, 4, 0, 0, FB, want, 0, 0, &st);
    if (reads != 0)
        *reads = bus.reads;
    return st.verdict;
}

#define NMACHINE ((unsigned int)(sizeof(machine) / sizeof(machine[0])))

static void
test_real_shape(void)
{
    unsigned long reads = 0UL;

    /* The machine as recorded: nothing in the way of 32 MB. */
    expect(run(machine, NMACHINE, WANT32, &reads) == OSMGA_SURVEY_CLEAR,
           "machine-is-clear-for-32mb");
    /* Bus 4 is only reachable by following 00:1e.0 to bus 3 and 03:0d.0 to
     * bus 4.  If the walk did not follow bridges it would never see the MGA,
     * and "clear" would be an accident. */
    expect(reads > 0UL, "walk-issued-reads");

    /*
     * Unassigned registers are not claims.  Every function on this bus has
     * several base registers left at zero and both bridges have an
     * unconfigured non-prefetchable window; if those counted, an
     * unconfigured bridge would read as a one-megabyte window at physical
     * zero and the log would bury the four claims that matter under a dozen
     * that say nothing.
     */
    {
        FakeBus b;
        OSMGASurveyState st;

        b.fns = machine;
        b.count = NMACHINE;
        b.reads = 0UL;
        OSMGAPciSurveyRun(fake_read, &b, 4, 0, 0, FB, WANT32, 0, 0, &st);
        /*
         * Exactly two: the prefetchable forwarding windows of 00:1e.0 and
         * 03:0d.0, which are the two ancestors between us and bus 0.  The
         * MGA's own three base registers are ours and not judged, and every
         * other register on this bus is zero.
         */
        expect(st.claimsSeen == 2UL, "only-assigned-registers-are-claims");
    }
}

static void
test_self_is_not_judged(void)
{
    FakeFn t[NMACHINE];
    unsigned int i;

    for (i = 0U; i < NMACHINE; i++)
        t[i] = machine[i];
    /* Our own BAR0 is inside our own range by definition.  Judging ourselves
     * would refuse every machine. */
    expect(run(t, NMACHINE, WANT32, 0) == OSMGA_SURVEY_CLEAR,
           "self-bar-not-judged");
}

static void
test_neighbour_inside(void)
{
    FakeFn t[NMACHINE + 1];
    unsigned int i;

    for (i = 0U; i < NMACHINE; i++)
        t[i] = machine[i];
    /* A perfectly ordinary device that happens to sit at 0xf9000000. */
    t[NMACHINE].bus = 0; t[NMACHINE].dev = 9; t[NMACHINE].fn = 0;
    t[NMACHINE].id = 0x11111111UL; t[NMACHINE].headerByte = 0x00UL;
    for (i = 0U; i < 6U; i++) t[NMACHINE].bar[i] = 0UL;
    t[NMACHINE].bar[0] = 0xf9000000UL;
    t[NMACHINE].rom = 0UL; t[NMACHINE].buses = 0UL;
    t[NMACHINE].memwin = 0UL; t[NMACHINE].prefwin = 0UL;
    t[NMACHINE].prefhi = 0UL;
    expect(run(t, NMACHINE + 1U, WANT32, 0) == OSMGA_SURVEY_BAR_INSIDE,
           "neighbour-inside-refuses-32mb");
    /* ...and the same machine is fine for a 16 MB declaration. */
    expect(run(t, NMACHINE + 1U, 16UL * MB, 0) == OSMGA_SURVEY_CLEAR,
           "neighbour-inside-does-not-affect-16mb");

    /* The case the alignment bound would have got wrong: a device below us
     * whose base has a 256 MiB alignment. */
    t[NMACHINE].bar[0] = 0xf0000000UL;
    expect(run(t, NMACHINE + 1U, WANT32, 0) == OSMGA_SURVEY_CLEAR,
           "neighbour-below-us-is-clear");

    /*
     * The same offender, but on bus 4 -- reachable only by following
     * 00:1e.0 to bus 3 and then 03:0d.0 to bus 4.
     *
     * Without this the suite passes even if the walk never follows a bridge
     * at all: every other case puts its offender on bus 0, and a walk that
     * stops at bus 0 finds nothing and reports "clear", which looks like
     * success.  This is the test that makes "clear" mean something.
     */
    t[NMACHINE].bus = 4;
    t[NMACHINE].dev = 5;
    t[NMACHINE].bar[0] = 0xf9000000UL;
    expect(run(t, NMACHINE + 1U, WANT32, 0) == OSMGA_SURVEY_BAR_INSIDE,
           "neighbour-two-bridges-deep-is-found");
}

static void
test_parent_window(void)
{
    FakeFn t[NMACHINE];
    unsigned int i;

    for (i = 0U; i < NMACHINE; i++)
        t[i] = machine[i];
    /* Our own bridge forwards only 16 MiB: the aperture cannot be 32. */
    t[2].prefwin = WIN_TO_F9;
    expect(run(t, NMACHINE, WANT32, 0) == OSMGA_SURVEY_PARENT_TOO_SMALL,
           "parent-window-stops-short");
    expect(run(t, NMACHINE, 16UL * MB, 0) == OSMGA_SURVEY_CLEAR,
           "parent-window-is-enough-for-16mb");

    /* A 64-bit prefetchable window with a non-zero upper half is out of a
     * 32-bit range's reach and must not be read as a 32-bit one. */
    t[2].prefwin = WIN_TO_F9 | 0x00010001UL;   /* type bits = 1 both halves */
    t[2].prefhi = 1UL;
    expect(run(t, NMACHINE, WANT32, 0) == OSMGA_SURVEY_CLEAR,
           "parent-64-bit-window-above-4gib-ignored");

    /* A window type this driver does not know is refused, not guessed. */
    t[2].prefwin = WIN_TO_FA | 0x00020002UL;
    t[2].prefhi = 0UL;
    expect(run(t, NMACHINE, WANT32, 0) == OSMGA_SURVEY_MALFORMED,
           "unknown-window-type-refused");
}

static void
test_foreign_bridge(void)
{
    FakeFn t[NMACHINE];
    unsigned int i;

    for (i = 0U; i < NMACHINE; i++)
        t[i] = machine[i];
    /* 00:1e.0 is an ancestor (sec=3 sub=4 contains bus 4).  Make it forward
     * our range to somewhere that is NOT our bus by moving us out of its
     * range: then the same window is a foreign claim on our addresses. */
    t[1].buses = 0x00020100UL;              /* sec=1 sub=2: not our ancestor */
    expect(run(t, NMACHINE, WANT32, 0) == OSMGA_SURVEY_WINDOW_OVERLAPS,
           "foreign-bridge-window-refuses");
}

static void
test_rom_and_bars(void)
{
    FakeFn t[NMACHINE + 1];
    unsigned int i;

    for (i = 0U; i < NMACHINE; i++)
        t[i] = machine[i];
    t[NMACHINE].bus = 0; t[NMACHINE].dev = 9; t[NMACHINE].fn = 0;
    t[NMACHINE].id = 0x11111111UL; t[NMACHINE].headerByte = 0x00UL;
    for (i = 0U; i < 6U; i++) t[NMACHINE].bar[i] = 0UL;
    t[NMACHINE].buses = 0UL; t[NMACHINE].memwin = 0UL;
    t[NMACHINE].prefwin = 0UL; t[NMACHINE].prefhi = 0UL;

    /* A DISABLED expansion ROM inside the aperture decodes nothing. */
    t[NMACHINE].rom = 0xf9000800UL;
    expect(run(t, NMACHINE + 1U, WANT32, 0) == OSMGA_SURVEY_CLEAR,
           "disabled-rom-ignored");
    /* Enabled, it is a claim like any other. */
    t[NMACHINE].rom = 0xf9000801UL;
    expect(run(t, NMACHINE + 1U, WANT32, 0) == OSMGA_SURVEY_BAR_INSIDE,
           "enabled-rom-inside-refuses");
    t[NMACHINE].rom = 0UL;

    /* An I/O BAR whose "address" would land inside the aperture decodes no
     * memory and must be skipped -- bit 0 is what says so. */
    t[NMACHINE].bar[0] = 0xf9000001UL;
    expect(run(t, NMACHINE + 1U, WANT32, 0) == OSMGA_SURVEY_CLEAR,
           "io-bar-ignored");

    /* A 64-bit pair: low dword type bits = 2, upper dword non-zero puts it
     * above four gigabytes, so it cannot touch a 32-bit range. */
    t[NMACHINE].bar[0] = 0xf9000004UL;
    t[NMACHINE].bar[1] = 1UL;
    expect(run(t, NMACHINE + 1U, WANT32, 0) == OSMGA_SURVEY_CLEAR,
           "64-bit-bar-above-4gib-ignored");
    /* Same pair with a zero upper half IS in our range, and the second dword
     * must not be re-read as a base of its own. */
    t[NMACHINE].bar[1] = 0UL;
    expect(run(t, NMACHINE + 1U, WANT32, 0) == OSMGA_SURVEY_BAR_INSIDE,
           "64-bit-bar-below-4gib-refuses");

    /*
     * A 64-bit pair whose UPPER dword would itself land inside the aperture
     * if it were mistaken for a base of its own.
     *
     * Without this, a walk that treats a 64-bit BAR as one dword instead of
     * two passes the whole suite: the earlier pair has a zero upper half, so
     * misreading it changes nothing.  Here the low half is below us and the
     * high half is 0xf9000000, so the two readings give opposite answers.
     */
    t[NMACHINE].bar[0] = 0xe0000004UL;
    t[NMACHINE].bar[1] = 0xf9000000UL;
    expect(run(t, NMACHINE + 1U, WANT32, 0) == OSMGA_SURVEY_CLEAR,
           "64-bit-upper-half-is-not-a-base");

    /* And the low half of that same pair, if the upper half were ignored
     * rather than making it out of reach, would be judged on its own. */
    t[NMACHINE].bar[0] = 0xf9000004UL;
    t[NMACHINE].bar[1] = 0x00000001UL;
    expect(run(t, NMACHINE + 1U, WANT32, 0) == OSMGA_SURVEY_CLEAR,
           "64-bit-low-half-not-judged-when-above-4gib");
    t[NMACHINE].bar[1] = 0UL;

    /* Our own 64-bit BAR is still ours and still not judged. */
    {
        FakeFn u[NMACHINE];
        unsigned int k;

        for (k = 0U; k < NMACHINE; k++)
            u[k] = machine[k];
        u[3].bar[0] = 0xf8000004UL;    /* the MGA's own, as a 64-bit pair */
        u[3].bar[1] = 0UL;
        expect(run(u, NMACHINE, WANT32, 0) == OSMGA_SURVEY_CLEAR,
               "self-64-bit-bar-not-judged");
    }

    /* A 64-bit BAR in the LAST slot has no second dword: refused, never
     * bounded. */
    t[NMACHINE].bar[0] = 0UL;
    t[NMACHINE].bar[1] = 0UL;
    t[NMACHINE].bar[5] = 0xe0000004UL;
    expect(run(t, NMACHINE + 1U, WANT32, 0) == OSMGA_SURVEY_MALFORMED,
           "64-bit-bar-in-last-slot-refused");
    t[NMACHINE].bar[5] = 0UL;

    /* Reserved BAR type 3. */
    t[NMACHINE].bar[0] = 0xe0000006UL;
    expect(run(t, NMACHINE + 1U, WANT32, 0) == OSMGA_SURVEY_MALFORMED,
           "reserved-bar-type-refused");
}

static void
test_header_and_functions(void)
{
    FakeFn t[NMACHINE + 2];
    unsigned int i;

    for (i = 0U; i < NMACHINE; i++)
        t[i] = machine[i];

    /* A Cardbus header is a layout this driver cannot parse. */
    t[NMACHINE].bus = 0; t[NMACHINE].dev = 9; t[NMACHINE].fn = 0;
    t[NMACHINE].id = 0x11111111UL; t[NMACHINE].headerByte = 0x02UL;
    for (i = 0U; i < 6U; i++) t[NMACHINE].bar[i] = 0UL;
    t[NMACHINE].rom = 0UL; t[NMACHINE].buses = 0UL;
    t[NMACHINE].memwin = 0UL; t[NMACHINE].prefwin = 0UL;
    t[NMACHINE].prefhi = 0UL;
    expect(run(t, NMACHINE + 1U, WANT32, 0) == OSMGA_SURVEY_UNKNOWN_HEADER,
           "cardbus-header-refused");

    /* Function 1 exists but function 0 does not advertise multifunction:
     * it must not be reached, because the bit is what says it is there. */
    t[NMACHINE].headerByte = 0x00UL;
    t[NMACHINE + 1] = t[NMACHINE];
    t[NMACHINE + 1].fn = 1;
    t[NMACHINE + 1].bar[0] = 0xf9000000UL;
    expect(run(t, NMACHINE + 2U, WANT32, 0) == OSMGA_SURVEY_CLEAR,
           "function-1-hidden-without-multifunction-bit");
    /* With the bit set it is reached, and its BAR counts. */
    t[NMACHINE].headerByte = 0x80UL;
    expect(run(t, NMACHINE + 2U, WANT32, 0) == OSMGA_SURVEY_BAR_INSIDE,
           "function-1-found-with-multifunction-bit");
}

int
main(void)
{
    test_real_shape();
    test_self_is_not_judged();
    test_neighbour_inside();
    test_parent_window();
    test_foreign_bridge();
    test_rom_and_bars();
    test_header_and_functions();
    if (failures != 0)
        return 1;
    printf("OPENSTEP_MGA_PCI_SURVEY_TEST=pass\n");
    return 0;
}
