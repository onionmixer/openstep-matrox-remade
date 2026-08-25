/*
 * The five waits a 3D submission spends, and the two settings that change
 * what it spends them on.
 *
 *   waits              print the counters
 *   waits <us> <pack>  set the completion-poll delay (0/1/2/4) and whether
 *                      an untextured trapezoid's FXBNDRY rides in its
 *                      execute block (0/1), then print the counters
 *
 * The counters are cumulative since the driver loaded.  Read them before a
 * measured run and after it, and subtract; a raw total says nothing.
 */
#import <driverkit/IODeviceMaster.h>
#import <driverkit/return.h>
#import <stdio.h>
#import <stdlib.h>

#define WAITS_PARAM  "OSMGAHW3DWaits"
#define TUNE_PARAM   "OSMGAHW3DTune"
#define WAITS_COUNT  16U

static const char *waitName[5] = {
    "pre-idle    ", "fifo admit  ", "quiescence  ",
    "completion  ", "final idle  "
};

int
main(int argc, char **argv)
{
    IODeviceMaster *master;
    IOObjectNumber objNum;
    IOString kind;
    unsigned w[WAITS_COUNT], n = WAITS_COUNT;
    unsigned i;
    IOReturn r;

    master = [IODeviceMaster new];
    if ([master lookUpByDeviceName:"Display0" objectNumber:&objNum
            deviceKind:&kind] != IO_R_SUCCESS) {
        printf("Display0 not found\n");
        return 1;
    }

    if (argc >= 3) {
        unsigned t[2];

        t[0] = (unsigned)atoi(argv[1]);
        t[1] = (unsigned)atoi(argv[2]);
        r = [master setIntValues:t forParameter:TUNE_PARAM
                    objectNumber:objNum count:2];
        if (r != IO_R_SUCCESS) {
            printf("tune refused (%d): delay must be 0, 1, 2 or 4 and pack "
                   "0 or 1\n", (int)r);
            return 2;
        }
        printf("tune: completion poll delay %u us, pack %u\n", t[0], t[1]);
    }

    r = [master getIntValues:w forParameter:WAITS_PARAM
                objectNumber:objNum count:&n];
    if (r != IO_R_SUCCESS) {
        printf("waits parameter unavailable (%d) -- is the new driver "
               "loaded?  It needs a reboot after install.\n", (int)r);
        return 3;
    }
    printf("waits telemetry version %u\n", w[0]);
    printf("  %-12s %10s %12s %10s %10s\n",
           "wait", "entered", "reads", "mean", "largest");
    for (i = 0U; i < 5U; i++) {
        unsigned cnt = w[1U + i * 3U];
        unsigned sum = w[2U + i * 3U];
        unsigned mx  = w[3U + i * 3U];

        printf("  %-12s %10u %12u %10.2f %10u\n",
               waitName[i], cnt, sum,
               cnt ? (double)sum / (double)cnt : 0.0, mx);
    }
    return 0;
}
