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
#define FENCE_PARAM  "OSMGAHW3DFence"
#define FENCE_COUNT  8U
#define TUNE_PARAM   "OSMGAHW3DTune"
#define SETTLE_PARAM "OSMGAHW3DSettle"
#define INJECT_PARAM "OSMGAHW3DInject"
#define WAITS_COUNT  25U

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

    /*
     * The settling read, as a sixth argument.
     *
     * After a batch the driver may read one word of video memory through an
     * UNCACHED alias, to give the engine's last writes somewhere to land
     * before software looks.  That read is outside all five bounded waits,
     * so if it is what a hang is made of, no give-up counter can see it --
     * and it is the one such access with a live switch.  0 turns it off.
     */
    if (argc >= 6) {
        unsigned st[1];

        st[0] = (unsigned)atoi(argv[5]);
        r = [master setIntValues:st forParameter:SETTLE_PARAM
                    objectNumber:objNum count:1];
        if (r != IO_R_SUCCESS) {
            printf("settle refused (%d): 0 turns the read off\n", (int)r);
            return 5;
        }
        printf("settle: the post-batch video-memory read is %s\n",
               st[0] ? "on" : "OFF");
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
    printf("  at entry: trap already set %u time(s), last status %05x\n",
           w[23], w[24]);
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
        /*
     * W4 A0.  Absent on a driver built without OSMGA_HW3D_FENCE_OBSERVE, and
     * that is the normal case -- a shipped driver does not carry the observer.
     * So a refusal here is reported as "not built in", not as an error.
     */
    {
        unsigned f[FENCE_COUNT], fn = FENCE_COUNT;

        if ([master getIntValues:f forParameter:FENCE_PARAM
                    objectNumber:objNum count:&fn] != IO_R_SUCCESS) {
            printf("\nfence observer: not built in (no OSMGA_HW3D_FENCE_OBSERVE)\n");
        } else {
            printf("\nfence observer version %u\n", f[0]);
            printf("  observed   %10u\n", f[1]);
            printf("  head == published end %10u\n", f[2]);
            printf("  head past  %10u\n", f[3]);
            printf("  head short %10u\n", f[4]);
            printf("  skipped (scissor present) %10u\n", f[5]);
            if (f[3] != 0U || f[4] != 0U)
                printf("  FIRST DISAGREEMENT head 0x%08x end 0x%08x\n",
                       f[6], f[7]);
            else
                printf("  no disagreement recorded\n");
        }
    }

return 0;
}
