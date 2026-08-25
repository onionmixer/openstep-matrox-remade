/*
 * The five waits a 3D submission spends, and the two settings that change
 * what it spends them on.
 *
 *   waits              print the counters
 *   waits <us> <pack> [track]
 *                      set the completion-poll delay (0/1/2/4), whether an
 *                      untextured trapezoid's FXBNDRY rides in its execute
 *                      block (0/1), and whether the colour and alpha blocks
 *                      are written only when they change (0/1), then print
 *                      the counters
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
#define INJECT_PARAM "OSMGAHW3DInject"
#define WAITS_COUNT  23U

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
        unsigned t[3];
        unsigned n3 = (argc >= 4) ? 3U : 2U;

        t[0] = (unsigned)atoi(argv[1]);
        t[1] = (unsigned)atoi(argv[2]);
        t[2] = (argc >= 4) ? (unsigned)atoi(argv[3]) : 0U;
        r = [master setIntValues:t forParameter:TUNE_PARAM
                    objectNumber:objNum count:n3];
        if (r != IO_R_SUCCESS) {
            printf("tune refused (%d): delay must be 0, 1, 2 or 4, pack and "
                   "track 0 or 1\n", (int)r);
            return 2;
        }
        if (n3 == 3U)
            printf("tune: delay %u us, pack %u, track %u\n",
                   t[0], t[1], t[2]);
        else
            printf("tune: delay %u us, pack %u (track left alone)\n",
                   t[0], t[1]);
    }

    if (argc >= 5) {
        unsigned inj[1];

        inj[0] = (unsigned)atoi(argv[4]);
        r = [master setIntValues:inj forParameter:INJECT_PARAM
                    objectNumber:objNum count:1];
        if (r != IO_R_SUCCESS) {
            printf("inject refused (%d): 0 to 4\n", (int)r);
            return 4;
        }
        printf("inject: the next %u submission(s) will report a timeout "
               "they did not suffer\n", inj[0]);
    }

    r = [master getIntValues:w forParameter:WAITS_PARAM
                objectNumber:objNum count:&n];
    if (r != IO_R_SUCCESS) {
        printf("waits parameter unavailable (%d) -- is the new driver "
               "loaded?  It needs a reboot after install.\n", (int)r);
        return 3;
    }
    printf("waits telemetry version %u\n", w[0]);
    printf("  %-12s %10s %12s %10s %10s %10s\n",
           "wait", "entered", "reads", "mean", "largest", "gave up");
    for (i = 0U; i < 5U; i++) {
        unsigned cnt = w[1U + i * 3U];
        unsigned sum = w[2U + i * 3U];
        unsigned mx  = w[3U + i * 3U];

        printf("  %-12s %10u %12u %10.2f %10u %10u\n",
               waitName[i], cnt, sum,
               cnt ? (double)sum / (double)cnt : 0.0, mx, w[18U + i]);
    }
    printf("  recovery: %u saved, %u latched acceleration off\n",
           w[16], w[17]);
    /*
     * "gave up" is the column that matters after a machine has frozen.  A
     * wait that reached its limit used to return in silence and let the next
     * submission do it again; now it says so, turns acceleration off, and
     * leaves this count behind even if the log did not survive.
     */
    {
        unsigned k, any = 0U;

        for (k = 0U; k < 5U; k++) any += w[18U + k];
        if (any != 0U)
            printf("  A WAIT GAVE UP.  Acceleration is off until the next "
                   "boot; see /usr/adm/messages for 3-61.\n");
    }
    return 0;
}
