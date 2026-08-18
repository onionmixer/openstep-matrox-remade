/*
 * S3b-prep probe: read the driver's telemetry and, on request, ask it for one
 * rectangle copy.  docs/S3B_PREP_INSTRUMENTATION_PLAN.md.
 *
 * Build on the target:
 *   cc -O -Wall -o /tmp/osmga-stats openstep-mga-stats-probe.m -lDriver
 *
 * Usage:
 *   osmga-stats                                  read counters
 *   osmga-stats blit sx sy w h dx dy             one OSMGAProbeBlit request
 *
 * "Display0" is the lookup name: proven on this target in
 * docs/P1_DRIVERKIT_DISPLAY_QUERY.md (MatroxMGA and MatroxMGA0 both failed
 * with -704; Display0 returned object 20, kind "Linear Framebuffer").
 * Object numbers are boot-local, so enumeration is only a fallback.
 *
 * This deliberately uses the private "OSMGAProbeBlit" parameter rather than
 * the standard IODisplayDoBlit: displayDefs.h says callers must not use the
 * standard request unless IO_DISPLAY_CAN_BLIT is advertised, and we have not
 * advertised it.
 */

#import <driverkit/IODeviceMaster.h>
#import <driverkit/return.h>
#import <stdio.h>
#import <stdlib.h>
#import <string.h>

#define STATS_PARAM   "OSMGAStats"
#define STATS_COUNT   26
#define PROBE_PARAM   "OSMGAProbeBlit"
#define PROBE_COUNT   6

static const char *statName[STATS_COUNT] = {
    "statsVersion", "blitRequests", "blitOk", "blitNoop",
    "refusedDisabled", "refusedGeometry", "refusedBusy", "refusedPreExec",
    "postExecTimeout", "cursorShow", "cursorMove", "cursorHide",
    "cursorWhileStormBusy", "thin1px", "enterLinear", "revertVGA",
    "stormBlitReady", "stormBlitFailed",
    /* reject-only observation mode */
    "observeOnly", "obsRequests", "obsOverlap", "obsWouldBlitUp",
    "obsWouldBlitLeft", "obsCursorInterleaved", "obsMaxWxH(packed)",
    "obsMinDim"
};

static int
findDisplay(IODeviceMaster *master, IOObjectNumber *objNum)
{
    IOString kind;
    IOReturn r;

    r = [master lookUpByDeviceName:"Display0" objectNumber:objNum
                        deviceKind:&kind];
    if (r == IO_R_SUCCESS) {
        printf("lookup Display0 -> object=%u kind=%s\n",
               (unsigned)*objNum, kind);
        return 0;
    }
    printf("lookup Display0 failed r=%d; falling back to enumeration\n", (int)r);
    {
        IOObjectNumber n;
        IOString name;
        for (n = 0; n < 64; n++) {
            r = [master lookUpByObjectNumber:n deviceKind:&kind
                                  deviceName:&name];
            if (r != IO_R_SUCCESS)
                continue;
            printf("  object=%u kind=%s name=%s\n", (unsigned)n, kind, name);
            if (strcmp(kind, "Linear Framebuffer") == 0) {
                *objNum = n;
                return 0;
            }
        }
    }
    return -1;
}

int
main(int argc, char **argv)
{
    IODeviceMaster *master;
    IOObjectNumber objNum = 0;
    unsigned values[STATS_COUNT];
    unsigned count;
    IOReturn r;
    int i;

    master = [IODeviceMaster new];
    if (master == nil) {
        printf("OSMGA_PROBE result=no-device-master\n");
        return 1;
    }
    if (findDisplay(master, &objNum) != 0) {
        printf("OSMGA_PROBE result=display-not-found\n");
        return 1;
    }

    if (argc >= 2 && strcmp(argv[1], "info") == 0) {
        /* Read IODisplayInfo through the SAME parameter the window server
         * uses, and report flags.  IO_DISPLAY_CAN_BLIT is bit 5 (0x20).
         * flags sits at byte offset 96: 5 ints + void* + 2 enums + char[64]. */
        unsigned char info[512];
        unsigned n = sizeof(info);
        r = [master getCharValues:info forParameter:"IOGetDisplayInfo"
                     objectNumber:objNum count:&n];
        printf("OSMGA_DISPLAY_INFO result=%d bytes=%u\n", (int)r, n);
        if (r == IO_R_SUCCESS && n >= 100) {
            unsigned *w = (unsigned *)info;
            unsigned flags = w[24];
            printf("  width=%u height=%u totalWidth=%u rowBytes=%u\n",
                   w[0], w[1], w[2], w[3]);
            printf("  bitsPerPixel=%u colorSpace=%u\n", w[6], w[7]);
            printf("  flags=0x%08x  CAN_BLIT(0x20)=%s  HAS_XFER_TABLE(0x10)=%s\n",
                   flags,
                   (flags & 0x20) ? "SET" : "clear",
                   (flags & 0x10) ? "SET" : "clear");
        }
    }

    if (argc >= 8 && strcmp(argv[1], "blit") == 0) {
        unsigned blit[PROBE_COUNT];
        for (i = 0; i < PROBE_COUNT; i++)
            blit[i] = (unsigned)strtoul(argv[i + 2], (char **)0, 0);
        printf("blit src=(%u,%u) size=%ux%u dst=(%u,%u)\n",
               blit[0], blit[1], blit[2], blit[3], blit[4], blit[5]);
        r = [master setIntValues:blit forParameter:PROBE_PARAM
                    objectNumber:objNum count:PROBE_COUNT];
        printf("OSMGA_PROBE_BLIT result=%d %s\n", (int)r,
               (r == IO_R_SUCCESS) ? "SUCCESS" : "refused/failed");
    }

    count = STATS_COUNT;
    r = [master getIntValues:values forParameter:STATS_PARAM
                objectNumber:objNum count:&count];
    if (r != IO_R_SUCCESS) {
        printf("OSMGA_PROBE_STATS result=%d (failed)\n", (int)r);
        return 1;
    }
    printf("OSMGA_PROBE_STATS result=0 count=%u\n", count);
    for (i = 0; i < (int)count && i < STATS_COUNT; i++) {
        if (i == 24)
            printf("  %-22s %ux%u\n", "obsMaxW x obsMaxH",
                   values[i] >> 16, values[i] & 0xffffU);
        else
            printf("  %-22s %u\n", statName[i], values[i]);
    }
    return 0;
}
