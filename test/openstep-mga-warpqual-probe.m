/*
 * openstep-mga-warpqual-probe.m -- run the M3 WARP qualification on a
 * machine that is already up.
 *
 * The boot-time run keeps losing its own log.  M3's output is roughly
 * twenty-five lines emitted from enterLinearMode, which is early enough
 * that syslogd may not yet be draining, and the last two runs kept only
 * the tail: four lines of twenty-five on this one.  The verdicts survived
 * because the summary was put there deliberately, but T2's depth readback
 * and T3's four phase residuals are measurements, not verdicts, and they
 * went with the rest.
 *
 * So this pokes OSMGAWarpQual through IODeviceMaster instead, with the
 * system fully up.  If the hypothesis is right the same lines survive; if
 * they still vanish, the cause is not syslogd's start-up and the next move
 * is fewer lines rather than a longer pause between them.
 *
 * The driver refuses this parameter unless "WARP Depth Test" is Yes in the
 * instance table, so a machine that never opted in cannot be made to run
 * it from here.
 */
#import <driverkit/IODeviceMaster.h>
#import <driverkit/return.h>
#import <stdio.h>

#define WARPQUAL_PARAM "OSMGAWarpQual"

int
main(void)
{
    IODeviceMaster *master;
    IOObjectNumber objNum = 0;
    IOString kind;
    unsigned one = 1;
    IOReturn r;

    master = [IODeviceMaster new];
    if (master == nil) {
        printf("WARPQUAL result=no-device-master\n");
        return 1;
    }
    r = [master lookUpByDeviceName:"Display0" objectNumber:&objNum
                        deviceKind:&kind];
    if (r != IO_R_SUCCESS) {
        printf("WARPQUAL result=display-not-found r=%d\n", (int)r);
        return 1;
    }
    printf("WARPQUAL Display0 object=%u kind=%s\n", (unsigned)objNum, kind);

    r = [master setIntValues:&one forParameter:WARPQUAL_PARAM
                objectNumber:objNum count:1];
    printf("WARPQUAL result=%d %s\n", (int)r,
           (r == IO_R_SUCCESS) ? "ran -- read /usr/adm/messages"
                               : "refused (flag off, or driver too old)");
    return (r == IO_R_SUCCESS) ? 0 : 1;
}
